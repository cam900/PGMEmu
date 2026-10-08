# 0012: MCP over HTTP is served by cpp-httplib, JSON-lines over TCP by sockets of our own

**Status:** accepted

## Context

[0005](0005-one-control-api.md) puts two transports on the desktop application: JSON-lines on
TCP and MCP over HTTP, both attached to the machine on screen. Neither the core nor the libraries
of [0006](0006-dependencies.md) speak a network protocol, so something has to.

The two differ in what they need. JSON-lines on TCP is a socket that reads and writes lines. MCP's
Streamable HTTP transport is a POST of one JSON-RPC message to an endpoint, answered by one JSON
body or by nothing, which is HTTP/1.1 parsing: request lines, headers, content length, keep-alive.

Three ways were weighed:

- a small HTTP library for MCP, and sockets of our own for the lines;
- standalone Asio for both, and an HTTP parser of our own on top of it;
- no library, and both of our own.

## Decision

- **MCP over HTTP is served by [cpp-httplib](https://github.com/yhirose/cpp-httplib)**: MIT, a
  single header, fetched with `FetchContent` at a pinned tag as 0006's other tooling is. Only the
  header is used. Upstream's build is not, as it looks for OpenSSL, zlib and Brotli, which a server
  on the loopback interface needs none of.
- **JSON-lines over TCP runs on sockets of our own** (`pgm::server::TcpLineServer`): POSIX
  sockets, and Winsock on Windows, a few hundred lines.
- Both listen on 127.0.0.1 only. The HTTP server also turns away a browser's request whose
  `Origin` is not a local page, so that a page elsewhere cannot drive the emulator through a name
  that resolves to the loopback address.

This adds cpp-httplib to 0006's list of fetched libraries.

## Consequences

- The HTTP parsing the emulator relies on is a maintained library's, and the code of our own is
  the part that is plain: lines over a socket.
- The socket code is ours to keep portable. It is exercised on POSIX by the suite; Windows has no
  test of its own until CI runs there.
- Serving beyond the loopback interface, or TLS, would need this revisited.
