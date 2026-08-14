#include "unapinet_web.h"

#include <emscripten.h>

EM_JS(void, unapinet_web_set_device, (int enabled), {
    const bridge = globalThis.JS1983UnapiBridge;
    if (bridge) bridge.setDevice(Boolean(enabled));
});

EM_JS(void, unapinet_web_reset_channels, (void), {
    const bridge = globalThis.JS1983UnapiBridge;
    if (bridge) bridge.resetChannels();
});

EM_JS(int, unapinet_web_take_activity, (void), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge && bridge.takeActivity() ? 1 : 0;
});

EM_JS(int, unapinet_web_dns, (const char *host), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge && bridge.dns(UTF8ToString(host)) ? 1 : 0;
});

EM_JS(int, unapinet_web_tcp_open,
      (int slot, const char *host, u16 port, u8 flags), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge && bridge.tcpOpen(
      slot, UTF8ToString(host), port, flags
    ) ? 1 : 0;
});

EM_JS(int, unapinet_web_tcp_send,
      (int slot, const u8 *data, size_t length), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge && bridge.tcpSend(
      slot, HEAPU8.subarray(data, data + length)
    ) ? 1 : 0;
});

EM_JS(void, unapinet_web_tcp_close, (int slot), {
    const bridge = globalThis.JS1983UnapiBridge;
    if (bridge) bridge.tcpClose(slot);
});

EM_JS(int, unapinet_web_tcp_read,
      (int slot, u8 *data, size_t maximum), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge ? bridge.tcpRead(slot, HEAPU8, data, maximum) : -2;
});

EM_JS(int, unapinet_web_tcp_available, (int slot), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge ? bridge.tcpAvailable(slot) : -2;
});

EM_JS(int, unapinet_web_udp_open, (int slot, u16 local_port), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge && bridge.udpOpen(slot, local_port) ? 1 : 0;
});

EM_JS(int, unapinet_web_udp_send,
      (int slot, const u8 *address, u16 port,
       const u8 *data, size_t length), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge && bridge.udpSend(
      slot,
      HEAPU8.subarray(address, address + 4),
      port,
      HEAPU8.subarray(data, data + length)
    ) ? 1 : 0;
});

EM_JS(void, unapinet_web_udp_close, (int slot), {
    const bridge = globalThis.JS1983UnapiBridge;
    if (bridge) bridge.udpClose(slot);
});

EM_JS(int, unapinet_web_udp_read,
      (int slot, u8 *address, u16 *port,
       u8 *data, size_t maximum), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge
      ? bridge.udpRead(slot, HEAPU8, address, port, data, maximum)
      : -2;
});

EM_JS(int, unapinet_web_udp_available, (int slot), {
    const bridge = globalThis.JS1983UnapiBridge;
    return bridge ? bridge.udpAvailable(slot) : -2;
});
