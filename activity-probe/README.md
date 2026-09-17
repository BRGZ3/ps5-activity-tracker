# Playlog runtime

[Русская версия](README.ru.md)

`activity-probe` is the native PS5 runtime. It reads system events, maps App IDs
to `CUSA...`/`PPSA...`, tracks sessions, and serves the local dashboard on port
`12888`.

The dashboard's library page is served at `/library.html`; its read-only API is
`/api/installed-games`. The scanner reads the console title registry at
`/system_data/priv/mms/app.db` and accepts valid CUSA/PPSA rows regardless of
firmware-specific `categoryType` values. Each row is additionally checked
against content roots used by internal/external/portable installs. Persistent
`appmeta` metadata is not sufficient evidence because it can remain after a
title is removed; ShadowMount+ mount links under `/user/app/<TITLE_ID>` are
accepted. The API
returns `installed:true` for titles still present and `installed:false` for
previously registered titles that are now absent. Known media/system
applications and DLC (`addcont.db`) are not included. If the registry is
temporarily unavailable, internal, external and portable-game directories are
scanned as a fallback.

A `/user/app/<TITLE_ID>` staging directory containing only `sce_sys/param.sfo`
or `param.json` is not considered live. The content root must expose
`eboot.bin`/`app.pkg`, or a `mount.lnk` whose target still exists.

When no games are returned, the API also reports `source`, `database_status`
and `database_rows`. These fields distinguish an actually empty registry from
an unavailable/locked database and are useful when collecting a diagnostic
report from a console.

## Build

```bash
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk make
```

The SDK must also contain the static `ps5-payload-sqlite` package under
`/user/homebrew` (`sqlite3.h` and `libsqlite3.a`). Set `SQLITE_PREFIX` when the
package is installed in a different sysroot location.

The primary result is `activity-probe.elf`. ELF and plugin files are generated
artifacts and are excluded from Git.

## One-time launch

Make sure the loader accepts a payload on port `9021`, then run:

```bash
cat activity-probe.elf | nc <PS5_IP> 9021
```

Open the Playlog Media tile and finish first-run setup. The runtime waits five
seconds before installing the selected autostart method.

## Autostart

For etaHEN:

```text
/data/etaHEN/plugins/Playlog.elf
/data/etaHEN/plugins/Playlog.elf.auto_start
```

For ShadowMount+/PLK:

```text
/data/ps5_autoloader/Playlog.elf
/data/ps5_autoloader/autoload.txt
```

The release uses one mode at a time. Do not leave an old
`ps5-activity-tracker.plugin`, raw ELF and SM+ autoload entry enabled together.

## Data

```text
/data/ps5-activity/summary.json
/data/ps5-activity/tracker-state.bin
/data/ps5-activity/tracker-state.prev.bin
/data/ps5-activity/completed-state.bin
/data/ps5-activity/config.json
/data/ps5-activity/covers/
/data/ps5-activity/backups/
```

Game totals, sessions, completion marks, names and cached covers remain in the
Playlog history when a game is removed from the console. The library's second
section is independent of that history and represents the system registry.

Playlog also watches the `SceShellUI` PID. If the shell is replaced while the
runtime remains alive, the stale AppFocus handle is closed and reopened. The
runtime does not subscribe to system power-transition events or stop itself
during suspend/shutdown; the console owns that lifecycle so the payload cannot
hold the system on a blue-light shutdown path.

The `/dev/klog` reader is self-healing: transient EOF/read errors close and
reopen the device in the background instead of terminating the runtime. The
HTTP writer uses `MSG_NOSIGNAL`, so a browser that disconnects during a large
response cannot kill Playlog with `SIGPIPE`. If the listener itself exits, the
main loop retries the LAN server automatically.

Game icons are resolved from conventional appmeta/mounted-content paths first,
then from the read-only `icon0Info`/`metaDataPath` fields in `app.db` (including
the shell's `?ts=...` suffix). A copy is kept under `/data/ps5-activity/covers`.

`probe-events.jsonl` may also be present and grows without rotation.
Do not attach it to a public issue together with personal history.

## Limitations

- primary hardware test: firmware 4.50;
- PS5 portable games do not always emit an explicit exit event;
- title lookup is best effort;
- timezone falls back to `Europe/Moscow` before setup;
- process and autostart modes must be kept mutually exclusive;
- updates do not promise rollback for every power-loss or partial-write case.

Known runtime limitations are listed in [../KNOWN_LIMITATIONS.md](../KNOWN_LIMITATIONS.md).
