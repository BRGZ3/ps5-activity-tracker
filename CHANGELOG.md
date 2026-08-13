# Changelog

[Русская версия](CHANGELOG.ru.md)

## 1.0.1-beta.2 — 2026-08-14

- tracks foreground and Home transitions through `SceShellCoreUtilAppFocus`;
- keeps the legacy `/dev/klog` focus parser as a fallback for older firmware;
- addresses zero-session tracking observed on firmware 11.60, where the
  legacy `FG App was changed` line is absent;
- ignores duplicate lifecycle transitions reported by both sources.

## 1.0.1-beta.1 — 2026-08-12

- no longer creates `/data/ps5_autoloader/autoload.txt` when it is missing;
- leaves empty, comment-only and directive-only autoload configurations unchanged;
- appends Playlog only when an existing configuration already contains another payload;
- preserves the user's chosen position when Playlog is already listed;
- warns when Playlog is the only payload in the existing configuration;
- reports when manual Payload Manager/autoload configuration is required.

## 1.0.0 — 2026-08-06

First public Playlog / PS5 Activity Tracker release.

Includes:

- PS4 (`CUSA...`) and PS5 (`PPSA...`) activity tracking;
- active time, pause time, game sessions and today/week/month periods;
- best-effort game-title lookup;
- reversible completion marks;
- local RU/EN dashboard with LAN read-only viewing;
- dashboard backup/restore and offline carrier updates;
- one raw ELF for etaHEN and ShadowMount+/PLK in the tested hardware scenarios.

This is a complete v1.0 release with known operational limitations. They are
listed in `KNOWN_LIMITATIONS.md`.
