# Security and privacy

[Русская версия](SECURITY.ru.md)

Playlog is intended for personal use on a hacked PS5. It does not connect to
PSN and does not send data to external services.

## Important release limitations

- The HTTP server uses plain HTTP without encryption.
- LAN view exposes game history and session times to any client that can reach
  the console port.
- Do not publish port `12888` to the Internet or forward it through a router.
- Create a dashboard backup before testing on important statistics.
- Do not run two Playlog runtimes or leave etaHEN and ShadowMount+/PLK
  autostarts enabled at the same time.

## Vulnerability reports

Do not publish details of a potential vulnerability in an open issue before a
fix is available. Use a private GitHub Security Advisory or the maintainer's
private contact channel and include:

- release version;
- firmware and autostart method;
- minimal reproduction steps;
- logs with IP addresses, profile names and user history removed.
