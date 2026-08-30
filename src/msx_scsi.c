#include "msx_scsi.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define IC_DBUS 0x01u
#define IC_ATN  0x02u
#define IC_SEL  0x04u
#define IC_BSY  0x08u
#define IC_ACK  0x10u
#define IC_LA   0x20u
#define IC_AIP  0x40u
#define IC_RST  0x80u

#define MODE_ARBITRATE 0x01u
#define MODE_DMA       0x02u
#define MODE_BSY_IRQ   0x04u
#define MODE_EOP_IRQ   0x08u
#define MODE_PARITY_IRQ 0x10u
#define MODE_PARITY    0x20u
#define MODE_TARGET    0x40u
#define MODE_BLOCK_DMA 0x80u

#define BUS_DBP 0x01u
#define BUS_SEL 0x02u
#define BUS_IO  0x04u
#define BUS_CD  0x08u
#define BUS_MSG 0x10u
#define BUS_REQ 0x20u
#define BUS_BSY 0x40u
#define BUS_RST 0x80u

#define BAS_ACK          0x01u
#define BAS_ATN          0x02u
#define BAS_BUSY_ERROR   0x04u
#define BAS_PHASE_MATCH  0x08u
#define BAS_IRQ          0x10u
#define BAS_PARITY_ERROR 0x20u
#define BAS_DMA_REQUEST  0x40u
#define BAS_END_DMA      0x80u

static bool trace_enabled(void) {
    const char *value = getenv("MSX_SCSI_TRACE");

    return value && value[0] && strcmp(value, "0") != 0;
}

static void controller_clear(MsxScsi *scsi) {
    scsi->output_data = 0;
    scsi->initiator_command = 0;
    scsi->mode = 0;
    scsi->target_command = 0;
    scsi->select_enable = 0;
    scsi->input_data = 0;
    scsi->status_latch = 0;
    scsi->bus_phase = SCSI_PHASE_BUS_FREE;
    scsi->dma_direction = MSX_SCSI_DMA_NONE;
    scsi->target_selected = false;
    scsi->selection_wait = false;
    scsi->request = false;
    scsi->acknowledge = false;
    scsi->dma_request = false;
    scsi->interrupt_request = false;
    scsi->lost_arbitration = false;
    scsi->arbitration_in_progress = false;
    scsi->test_mode = false;
    scsi->pending_input_advance = false;
    scsi_disk_bus_reset(&scsi->disk);
}

static bool target_phase_active(const MsxScsi *scsi) {
    return scsi->target_selected &&
           scsi->bus_phase != SCSI_PHASE_BUS_FREE;
}

static bool phase_matches(const MsxScsi *scsi) {
    return target_phase_active(scsi) &&
           (scsi->target_command & 7u) ==
               ((unsigned)scsi->bus_phase & 7u);
}

static void update_dma_request(MsxScsi *scsi) {
    ScsiPhase phase;

    scsi->dma_request = false;
    if (!(scsi->mode & MODE_DMA) || !scsi->request ||
        !phase_matches(scsi))
        return;
    phase = scsi->bus_phase;
    if (scsi->dma_direction == MSX_SCSI_DMA_RECEIVE &&
        scsi_disk_phase_is_input(phase)) {
        scsi->input_data = scsi_disk_current_byte(&scsi->disk);
        scsi->dma_request = true;
    } else if (scsi->dma_direction == MSX_SCSI_DMA_SEND &&
               scsi_disk_phase_is_output(phase)) {
        scsi->dma_request = true;
    }
}

static void phase_changed(MsxScsi *scsi, ScsiPhase previous) {
    ScsiPhase next = scsi_disk_phase(&scsi->disk);

    scsi->bus_phase = next;
    if (next == SCSI_PHASE_BUS_FREE) {
        scsi->target_selected = false;
        scsi->selection_wait = false;
        scsi->request = false;
        scsi->dma_request = false;
        scsi->dma_direction = MSX_SCSI_DMA_NONE;
        return;
    }
    scsi->request = !scsi->selection_wait;
    if ((scsi->mode & MODE_DMA) && previous != next &&
        (scsi->target_command & 7u) != ((unsigned)next & 7u))
        scsi->interrupt_request = true;
    update_dma_request(scsi);
}

static void target_select(MsxScsi *scsi) {
    u8 target_mask = (u8)(1u << scsi->target_id);

    if (scsi->target_selected || !scsi_disk_mounted(&scsi->disk) ||
        !(scsi->output_data & target_mask))
        return;
    scsi->target_selected = true;
    scsi->selection_wait = true;
    scsi->request = false;
    scsi_disk_select(&scsi->disk,
                     (scsi->initiator_command & IC_ATN) != 0);
    scsi->bus_phase = scsi_disk_phase(&scsi->disk);
}

static void selection_released(MsxScsi *scsi) {
    if (!scsi->target_selected || !scsi->selection_wait)
        return;
    scsi->selection_wait = false;
    scsi->request = true;
    update_dma_request(scsi);
}

static void programmed_ack_assert(MsxScsi *scsi) {
    ScsiPhase phase;

    if (!scsi->target_selected || !scsi->request)
        return;
    phase = scsi->bus_phase;
    scsi->acknowledge = true;
    scsi->request = false;
    if (scsi_disk_phase_is_output(phase))
        scsi_disk_accept_byte(&scsi->disk, scsi->output_data);
    else if (scsi_disk_phase_is_input(phase))
        scsi->pending_input_advance = true;
}

static void programmed_ack_release(MsxScsi *scsi) {
    ScsiPhase previous;

    if (!scsi->acknowledge)
        return;
    previous = scsi->bus_phase;
    scsi->acknowledge = false;
    if (scsi->pending_input_advance) {
        scsi->pending_input_advance = false;
        scsi_disk_advance_byte(&scsi->disk);
    }
    phase_changed(scsi, previous);
}

static u8 current_data(const MsxScsi *scsi) {
    if (target_phase_active(scsi) &&
        scsi_disk_phase_is_input(scsi->bus_phase))
        return scsi_disk_current_byte(&scsi->disk);
    if (scsi->initiator_command & IC_DBUS)
        return scsi->output_data;
    return 0;
}

static u8 current_bus_status(const MsxScsi *scsi) {
    u8 result = 0;

    if (scsi->initiator_command & IC_RST)
        result |= BUS_RST;
    if (scsi->target_selected ||
        (scsi->initiator_command & IC_BSY))
        result |= BUS_BSY;
    if (scsi->request)
        result |= BUS_REQ;
    if (target_phase_active(scsi)) {
        unsigned phase = (unsigned)scsi->bus_phase;

        if (phase & 4u)
            result |= BUS_MSG;
        if (phase & 2u)
            result |= BUS_CD;
        if (phase & 1u)
            result |= BUS_IO;
    }
    if (scsi->initiator_command & IC_SEL)
        result |= BUS_SEL;
    return result;
}

static u8 bus_and_status(const MsxScsi *scsi) {
    u8 result = scsi->status_latch &
                (BAS_BUSY_ERROR | BAS_PARITY_ERROR | BAS_END_DMA);

    if (scsi->acknowledge ||
        (scsi->initiator_command & IC_ACK))
        result |= BAS_ACK;
    if (scsi->initiator_command & IC_ATN)
        result |= BAS_ATN;
    if (phase_matches(scsi))
        result |= BAS_PHASE_MATCH;
    if (scsi->interrupt_request)
        result |= BAS_IRQ;
    if (scsi->dma_request)
        result |= BAS_DMA_REQUEST;
    return result;
}

static void dma_phase_complete(MsxScsi *scsi, ScsiPhase previous) {
    phase_changed(scsi, previous);
    if (scsi->target_selected && scsi->bus_phase != previous &&
        !phase_matches(scsi)) {
        scsi->interrupt_request = true;
        scsi->dma_request = false;
    }
}

static u8 dma_read(MsxScsi *scsi) {
    ScsiPhase previous = scsi->bus_phase;
    u8 value = scsi->input_data;

    if (!scsi->dma_request ||
        scsi->dma_direction != MSX_SCSI_DMA_RECEIVE)
        return value;
    scsi->dma_request = false;
    scsi_disk_advance_byte(&scsi->disk);
    dma_phase_complete(scsi, previous);
    return value;
}

static void dma_write(MsxScsi *scsi, u8 value) {
    ScsiPhase previous = scsi->bus_phase;

    if (!scsi->dma_request ||
        scsi->dma_direction != MSX_SCSI_DMA_SEND)
        return;
    scsi->dma_request = false;
    scsi->output_data = value;
    scsi_disk_accept_byte(&scsi->disk, value);
    dma_phase_complete(scsi, previous);
}

static u8 register_read(MsxScsi *scsi, unsigned reg) {
    switch (reg & 7u) {
        case 0:
            return current_data(scsi);
        case 1:
            return (scsi->initiator_command & 0x9fu) |
                   (scsi->lost_arbitration ? IC_LA : 0) |
                   (scsi->arbitration_in_progress ? IC_AIP : 0);
        case 2:
            return scsi->mode;
        case 3:
            return scsi->target_command;
        case 4:
            return current_bus_status(scsi);
        case 5:
            return bus_and_status(scsi);
        case 6:
            return scsi->input_data;
        case 7:
            scsi->interrupt_request = false;
            scsi->status_latch &=
                (u8)~(BAS_PARITY_ERROR | BAS_BUSY_ERROR);
            return 0;
    }
    return 0xff;
}

static void initiator_command_write(MsxScsi *scsi, u8 value) {
    u8 previous = scsi->initiator_command;

    if (value & IC_RST) {
        controller_clear(scsi);
        scsi->initiator_command = IC_RST;
        scsi->interrupt_request = true;
        return;
    }
    /* On the Z5380 bit 6 is AIP when read, but is the write-only test
     * (tri-state) control when written. Controller firmware detects the
     * chip by setting it and observing FFh on the now-floating CPU data
     * bus, then clears it with another write to this register. */
    scsi->test_mode = (value & IC_AIP) != 0;
    scsi->initiator_command = value & 0x9fu;
    if (!(previous & IC_SEL) && (value & IC_SEL))
        target_select(scsi);
    if ((previous & IC_SEL) && !(value & IC_SEL))
        selection_released(scsi);
    if (!(previous & IC_ACK) && (value & IC_ACK))
        programmed_ack_assert(scsi);
    if ((previous & IC_ACK) && !(value & IC_ACK))
        programmed_ack_release(scsi);
}

static void mode_write(MsxScsi *scsi, u8 value) {
    u8 previous = scsi->mode;

    scsi->mode = value;
    if (!(previous & MODE_ARBITRATE) && (value & MODE_ARBITRATE)) {
        scsi->lost_arbitration = false;
        scsi->arbitration_in_progress = true;
    } else if ((previous & MODE_ARBITRATE) &&
               !(value & MODE_ARBITRATE)) {
        scsi->arbitration_in_progress = false;
        scsi->lost_arbitration = false;
    }
    if ((previous & MODE_DMA) && !(value & MODE_DMA)) {
        scsi->dma_direction = MSX_SCSI_DMA_NONE;
        scsi->dma_request = false;
        scsi->status_latch &= (u8)~BAS_END_DMA;
    }
    if (!(value & MODE_BSY_IRQ))
        scsi->status_latch &= (u8)~BAS_BUSY_ERROR;
}

static void start_dma(MsxScsi *scsi, MsxScsiDmaDirection direction) {
    if (!(scsi->mode & MODE_DMA))
        return;
    scsi->dma_direction = direction;
    update_dma_request(scsi);
    if (scsi->request && !phase_matches(scsi))
        scsi->interrupt_request = true;
}

static void register_write(MsxScsi *scsi, unsigned reg, u8 value) {
    switch (reg & 7u) {
        case 0:
            scsi->output_data = value;
            /* The firmware asserts SEL before it adds the target ID to the
             * data bus.  A real 5380 keeps evaluating the selection bus while
             * SEL is asserted, so retry when the initiator data register
             * changes instead of only looking at the SEL rising edge. */
            if (scsi->initiator_command & IC_SEL)
                target_select(scsi);
            break;
        case 1:
            initiator_command_write(scsi, value);
            break;
        case 2:
            mode_write(scsi, value);
            break;
        case 3:
            scsi->target_command = value;
            update_dma_request(scsi);
            break;
        case 4:
            scsi->select_enable = value;
            break;
        case 5:
            start_dma(scsi, MSX_SCSI_DMA_SEND);
            break;
        case 6:
            /* Target-mode receive is unused by the MSX cartridge. */
            if ((scsi->mode & (MODE_DMA | MODE_TARGET)) ==
                    (MODE_DMA | MODE_TARGET))
                start_dma(scsi, MSX_SCSI_DMA_RECEIVE);
            break;
        case 7:
            if (!(scsi->mode & MODE_TARGET))
                start_dma(scsi, MSX_SCSI_DMA_RECEIVE);
            break;
    }
}

void msx_scsi_init(MsxScsi *scsi) {
    if (!scsi)
        return;
    memset(scsi, 0, sizeof(*scsi));
    scsi_disk_init(&scsi->disk);
    scsi->target_id = MSX_SCSI_DEFAULT_TARGET_ID;
    controller_clear(scsi);
}

void msx_scsi_destroy(MsxScsi *scsi) {
    if (!scsi)
        return;
    scsi_disk_destroy(&scsi->disk);
    free(scsi->rom);
    memset(scsi, 0, sizeof(*scsi));
}

void msx_scsi_reset(MsxScsi *scsi) {
    if (!scsi)
        return;
    scsi->rom_bank = 0;
    controller_clear(scsi);
}

int msx_scsi_install_rom(MsxScsi *scsi, const u8 *data, size_t size) {
    u8 *copy;

    if (!scsi || !data || !size ||
        size > MSX_SCSI_ROM_MAX_SIZE ||
        size % MSX_SCSI_ROM_BANK_SIZE != 0)
        return -1;
    copy = malloc(size);
    if (!copy)
        return -1;
    memcpy(copy, data, size);
    free(scsi->rom);
    scsi->rom = copy;
    scsi->rom_size = size;
    scsi->rom_loaded = true;
    msx_scsi_reset(scsi);
    return 0;
}

int msx_scsi_eject_rom(MsxScsi *scsi) {
    if (!scsi)
        return -1;
    if (msx_scsi_eject_disk(scsi) != 0)
        return -1;
    free(scsi->rom);
    scsi->rom = NULL;
    scsi->rom_size = 0;
    scsi->rom_loaded = false;
    msx_scsi_reset(scsi);
    return 0;
}

bool msx_scsi_rom_loaded(const MsxScsi *scsi) {
    return scsi && scsi->rom_loaded && scsi->rom;
}

void msx_scsi_set_target_id(MsxScsi *scsi, unsigned target_id) {
    if (!scsi)
        return;
    scsi->target_id = target_id < 7
                    ? target_id : MSX_SCSI_DEFAULT_TARGET_ID;
    msx_scsi_reset(scsi);
}

unsigned msx_scsi_target_id(const MsxScsi *scsi) {
    return scsi ? scsi->target_id : MSX_SCSI_DEFAULT_TARGET_ID;
}

int msx_scsi_mount_disk(MsxScsi *scsi, const char *path,
                        AtaImageMode mode) {
    if (!scsi || !msx_scsi_rom_loaded(scsi))
        return -1;
    return scsi_disk_mount(&scsi->disk, path, mode);
}

int msx_scsi_flush_disk(MsxScsi *scsi) {
    return scsi ? scsi_disk_flush(&scsi->disk) : -1;
}

int msx_scsi_eject_disk(MsxScsi *scsi) {
    return scsi ? scsi_disk_unmount(&scsi->disk) : -1;
}

bool msx_scsi_disk_mounted(const MsxScsi *scsi) {
    return scsi && scsi_disk_mounted(&scsi->disk);
}

bool msx_scsi_disk_writable(const MsxScsi *scsi) {
    return scsi && scsi_disk_writable(&scsi->disk);
}

bool msx_scsi_disk_dirty(const MsxScsi *scsi) {
    return scsi && scsi_disk_dirty(&scsi->disk);
}

bool msx_scsi_disk_has_error(const MsxScsi *scsi) {
    return scsi && scsi_disk_has_error(&scsi->disk);
}

const char *msx_scsi_disk_error(const MsxScsi *scsi) {
    return scsi ? scsi_disk_error(&scsi->disk) : "";
}

bool msx_scsi_take_activity(MsxScsi *scsi) {
    return scsi && scsi_disk_take_activity(&scsi->disk);
}

u8 msx_scsi_memory_read(MsxScsi *scsi, u16 address) {
    size_t offset;

    if (!msx_scsi_rom_loaded(scsi) ||
        address < 0x4000 || address >= 0x8000)
        return 0xff;
    offset = (size_t)scsi->rom_bank * MSX_SCSI_ROM_BANK_SIZE +
             (address - 0x4000u);
    if (trace_enabled() && scsi->trace_rom_reads < 32u) {
        fprintf(stderr, "[scsi] rom r %04X bank=%u -> %02X\n",
                address, scsi->rom_bank,
                offset < scsi->rom_size ? scsi->rom[offset] : 0xff);
        ++scsi->trace_rom_reads;
    }
    return offset < scsi->rom_size ? scsi->rom[offset] : 0xff;
}

void msx_scsi_memory_write(MsxScsi *scsi, u16 address, u8 value) {
    if (!msx_scsi_rom_loaded(scsi))
        return;
    if (address == 0x6000)
        scsi->rom_bank = value & 0x1fu;
    if (trace_enabled() && address == 0x6000 &&
        scsi->trace_bank_writes < 64u) {
        fprintf(stderr, "[scsi] rom bank <- %u\n", scsi->rom_bank);
        ++scsi->trace_bank_writes;
    }
}

bool msx_scsi_io_read(MsxScsi *scsi, u16 port, u8 *value) {
    u8 low = (u8)port;

    if (!msx_scsi_rom_loaded(scsi) || low < 0xd0 || low > 0xd7)
        return false;
    if (scsi->test_mode)
        *value = 0xff;
    else if (low == 0xd0 && scsi->dma_request &&
        scsi->dma_direction == MSX_SCSI_DMA_RECEIVE)
        *value = dma_read(scsi);
    else
        *value = register_read(scsi, low - 0xd0);
    if (trace_enabled() && scsi->trace_io_events < 65536u) {
        fprintf(stderr,
                "[scsi] in  %02X -> %02X phase=%u req=%u bsy=%u dma=%u\n",
                low, *value, (unsigned)scsi->bus_phase,
                scsi->request, scsi->target_selected,
                scsi->dma_request);
        ++scsi->trace_io_events;
    }
    return true;
}

bool msx_scsi_io_write(MsxScsi *scsi, u16 port, u8 value) {
    u8 low = (u8)port;

    if (!msx_scsi_rom_loaded(scsi) || low < 0xd0 || low > 0xd7)
        return false;
    if (low == 0xd0 && scsi->dma_request &&
        scsi->dma_direction == MSX_SCSI_DMA_SEND)
        dma_write(scsi, value);
    else
        register_write(scsi, low - 0xd0, value);
    if (trace_enabled() && scsi->trace_io_events < 65536u) {
        fprintf(stderr,
                "[scsi] out %02X <- %02X phase=%u req=%u bsy=%u dma=%u\n",
                low, value, (unsigned)scsi->bus_phase,
                scsi->request, scsi->target_selected,
                scsi->dma_request);
        ++scsi->trace_io_events;
    }
    return true;
}
