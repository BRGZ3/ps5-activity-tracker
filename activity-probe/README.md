# Playlog runtime

[Русская версия](README.ru.md)

`activity-probe` is the native PS5 runtime. It reads system events, maps App IDs
to `CUSA...`/`PPSA...`, tracks sessions, and serves the local dashboard on port
`12888`.

## Build

```bash
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk make
```

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
/data/ps5-activity/backups/
```

`probe-events.jsonl` may also be present and grows without rotation.
Do not attach it to a public issue together with personal history.

## Limitations

- primary hardware test: firmware 4.50;
- PS5 portable games do not always emit an explicit exit event;
- title lookup is best effort;
- timezone falls back to `Europe/Moscow` before setup;
- process and autostart modes must be kept mutually exclusive;
- v1.0 does not promise rollback for every partial update failure.

Known runtime limitations are listed in [../KNOWN_LIMITATIONS.md](../KNOWN_LIMITATIONS.md).
