# Compatibility matrix

The v1.0 release claims only configurations that have been exercised on
hardware. Add new rows with the exact firmware, loader/runtime version, date
and result.

| Firmware | Autostart | Runtime | Result | Notes |
| --- | --- | --- | --- | --- |
| 4.50 | etaHEN | `Playlog.elf` | tested | primary v1.0 target |
| 4.50 | ShadowMount+/PLK | `Playlog.elf` | tested | primary v1.0 target |

Unknown firmware versions are not automatically supported. In particular,
raw ELF status detection and `/data` ↔ `/user/data` path behavior can differ
between etaHEN releases.
