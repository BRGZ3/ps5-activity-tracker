# Known limitations

[Русская версия](KNOWN_LIMITATIONS.ru.md)

This first public v1.0 release still has operational limitations. It does not
guarantee that statistics survive a kernel panic, power loss, two concurrently
running runtimes or every failed update scenario.

Before the first launch:

1. save the existing `/data/ps5-activity` directory;
2. make sure only one Playlog runtime is running;
3. select only one autostart method;
4. keep port `12888` inside a trusted local network.

Current limitations:

- primary hardware testing was performed on firmware 4.50;
- PS5 portable games may end a session only when another game is launched or
  the runtime is restarted;
- a title may be missing and appear as `PPSA...` or `CUSA...`;
- statistics are not separated by console profile;
- error diagnostics and extended analytics are not included in v1.0;
- timezone uses a `Europe/Moscow` fallback before configuration;
- raw `probe-events.jsonl` is not rotated automatically.

Bug reports should include the release version, firmware, autostart mode and a
minimal reproduction. Do not attach personal summaries, backups or complete
logs.
