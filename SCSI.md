# MSX SCSI cartridge

1983 models the banked MSX SCSI interface built around the NCR/Z5380 family:
a 16 KiB ROM window at `4000h-7FFFh`, bank selection at `6000h`, and
controller registers plus pseudo-DMA at either `30h-37h` or `D0h-D7h`.
The base is user-selectable because both CPLD revisions exist. One raw host
file is exposed as a direct-access SCSI disk with 512-byte sectors. Target ID
0 is the default because it is the first device probed by BERT SCSI V2.7.

1983 includes both known controller revisions. The file names make their
required port ranges explicit:

- `ROMS/BertSCSI-v1-D0h-D7h.ROM` uses `D0h-D7h`;
- `ROMS/BertSCSI-v2-30h-37h.ROM` uses `30h-37h` and is the default.

Here v1/v2 identifies the controller I/O revision, not the ROM's internal
firmware banner. The images are included with permission from their author;
see `ROMS/README-MSXSCSI` for provenance and checksums. The extension reserves
one cartridge slot. The D0 setting cannot coexist with CDX-2 because the
devices overlap at `D0h-D4h`; the 30 setting has no such I/O conflict.

## Starting it

In the F9 overlay, enable **General > Extra Hardware**, select
**Extensions > MSX SCSI**, then choose:

- the controller firmware ROM;
- an optional raw disk image;
- target ID 0-6 (normally 0);
- I/O base `30h` or `D0h`, matching the CPLD and ROM revision.

The mounted disk subsequently appears under **Media > SCSI hard disk**.
Read-only is the safe default. With Tinker enabled, **Advanced > SCSI access
mode** enables read/write access. Delete safely ejects media after flushing
completed writes; reset discards an incomplete sector without writing it.

The equivalent command line is:

```sh
./1983 --model msx2 --msx-scsi \
  --scsi-rom ROMS/BertSCSI-v2-30h-37h.ROM \
  --scsi-disk /path/to/MSXDOS2-SCSI.IMG \
  --scsi-id 0 --scsi-port 30 --scsi-mode read-only
```

Use `--scsi-mode read-write` only when the guest must install or modify the
disk, and keep a backup of writable images. `MSX_SCSI_TRACE=1` prints bounded
controller and CDB diagnostics for troubleshooting.

## Preparing an MSX-DOS2 image for BERT SCSI

BERT SCSI V2.7 does not treat a conventional PC/Nextor type-06 FAT16 image as
its boot volume. Its firmware expects the BERT partition table convention and
a type-01 FAT12 DOS partition. The helper below creates a roughly 32 MiB raw
image with that layout:

```sh
sudo dnf install util-linux dosfstools mtools
tools/create-bert-scsi-image.sh MSXDOS2-SCSI.IMG \
  /path/to/MSXDOS2.SYS /path/to/COMMAND2.COM
```

1983 does not distribute the two Microsoft DOS files; supply copies you are
entitled to use. The resulting image has been verified through the emulated
cartridge by booting to `A>`, running `DIR`, and reading both files. To inspect
the partition from the host without mounting it:

```sh
mdir -i MSXDOS2-SCSI.IMG@@16384 ::
```

Running the helper with only the output filename creates the same blank FAT12
image without DOS files.

## Nextor images

The SCSI transport implements the SCSI-1 commands used by the tested BERT
firmware, including capacity, inquiry, sense, 6- and 10-byte sector reads and
writes, mode commands, and cache synchronization. A Nextor-capable controller
ROM can therefore use a raw image prepared for that ROM. Partition formats
are firmware policy, however: an image made for Sunrise IDE or SD Mapper is
not automatically a BERT SCSI boot image. The current deterministic boot
checkpoint is MSX-DOS2 with BERT SCSI V2.7.

Hardware background and board files are available from the independent
[SCSI interface for MSX computers](https://hackaday.io/project/205465-scsi-interface-for-msx-computers)
project.
