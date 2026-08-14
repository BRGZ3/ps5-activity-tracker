# Compatibility matrix

The release claims only configurations that have been exercised on
hardware. Add new rows with the exact firmware, loader/runtime version, date
and result.

| Firmware | Autostart | Runtime | Result | Notes |
| --- | --- | --- | --- | --- |
| 4.50 | etaHEN | `Playlog.elf` | tested | primary target |
| 4.50 | ShadowMount+/PLK | `Playlog.elf` | tested | primary target |
| 11.60 | ShadowMount+/PLK | `Playlog.elf` | user-tested | `SceShellCoreUtilAppFocus` session tracking and dashboard population verified for v1.1.0 |

Unknown firmware versions are not automatically supported. The legacy klog
parser and `SceShellCoreUtilAppFocus` monitor run together, but raw ELF status
detection and `/data` ↔ `/user/data` path behavior can still differ between
loader releases.
