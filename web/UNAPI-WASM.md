# TCP/IP UNAPI in the browser

The WebAssembly edition exposes an openMSXnet-compatible host device on MSX
I/O ports 28h and 29h. It is a port-mapped extension: enabling it does **not**
occupy cartridge I or II. Guest software still needs the usual `UNAPINET.COM`
driver before TCP/IP UNAPI applications such as GeoBench or SymbOS can discover
the network.

A browser cannot create arbitrary TCP or UDP sockets. Javascript 1983 therefore
uses this deliberately narrow path:

    MSX application -> UNAPINET.COM -> ports 28h/29h -> WASM bridge
                    -> WebSocket -> restricted relay -> TCP/UDP

DNS, outbound TCP clients, and outbound UDP are supported. Listening TCP
sockets and fixed local UDP ports are not exposed by the browser bridge.

## Run the browser application and relay locally

Node.js 20 or later is required:

    npm --prefix web/relay ci
    make -C web serve

Open `http://127.0.0.1:1983/`. This single process publishes the static files
from `web/dist` and accepts the `/unapi` WebSocket on the same address. The AUX
panel therefore reports **Relay online** as soon as the UNAPI extension is
enabled; its endpoint needs no manual editing.

UNAPI can be enabled for one page load without changing saved settings:

    http://127.0.0.1:1983/?extensions=unapi

It can be combined with startup media, including an SD Mapper image and floppy:

    http://127.0.0.1:1983/?extensions=unapi&sda=media/GBMSX.IMG&disk=media/GEOBENCH.DSK

If port 1983 is already occupied, choose another port for both services:

    make -C web serve UNAPI_PORT=19830

The frontend is still an ordinary static browser application. To serve it
separately, run only the relay on another port:

    UNAPI_SERVE_STATIC=0 UNAPI_RELAY_PORT=1984 npm --prefix web/relay start

Then set the AUX-panel endpoint to `ws://127.0.0.1:1984/unapi`, or supply it
without persisting it:

    http://127.0.0.1:1983/?unapiRelay=ws%3A%2F%2F127.0.0.1%3A1984%2Funapi

The browser initially proposes the same-origin `ws(s)://host/unapi` endpoint,
which is convenient when a reverse proxy serves the frontend and relay from
one origin. An HTTPS frontend must use a WSS relay.

## Relay policy

The relay is restrictive by default:

- it binds to loopback;
- it denies private, loopback, link-local, multicast, reserved and documentation
  destination addresses;
- outbound TCP is limited to ports 23, 70, 80, 443 and 2323;
- outbound UDP is limited to port 123;
- browser origin, connection, channel, payload, queue and idle limits are
  enforced;
- unsolicited UDP peers are ignored.

Relevant environment variables are:

| Variable | Purpose | Default |
| --- | --- | --- |
| `UNAPI_RELAY_HOST` | Listen address | `127.0.0.1` |
| `UNAPI_RELAY_PORT` | Listen port | `1983` |
| `UNAPI_RELAY_PATH` | WebSocket path | `/unapi` |
| `UNAPI_SERVE_STATIC` | Set to `0` for relay-only mode | enabled |
| `UNAPI_STATIC_ROOT` | Browser files served by the combined launcher | `web/dist` |
| `UNAPI_ORIGINS` | Comma-separated allowed origins | loopback origins |
| `UNAPI_TOKEN` | Optional URL query token | empty |
| `UNAPI_TCP_PORTS` | Comma-separated TCP allowlist | `23,70,80,443,2323` |
| `UNAPI_UDP_PORTS` | Comma-separated UDP allowlist | `123` |

Deploy the relay behind a TLS reverse proxy for public use. Keep the destination
allowlists narrow and set both `UNAPI_ORIGINS` and `UNAPI_TOKEN`; the relay is
not intended to be an unrestricted network proxy.

## Tests

The wire protocol and browser bridge tests do not require network access:

    node web/test_unapi_protocol.js
    node web/test_unapi_bridge.js

After installing relay dependencies, its integration tests exercise DNS, TCP,
UDP, origin checks, destination policy, and limits against local test sockets:

    npm --prefix web/relay test
