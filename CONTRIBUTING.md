# Contributing

[Русская версия](CONTRIBUTING.ru.md)

## Before submitting changes

```bash
python3 -m unittest discover -s tests -v
python3 tools/check_public_tree.py
node --check release-build/build-carrier.js
```

For runtime changes, also run `clang -fsyntax-only` with the release defines
when the PS5 Payload SDK is available.

## Console data

Do not attach `summary.json`, `probe-events.jsonl`, `tracker-state*.bin`,
backups or screenshots containing IP addresses or profile names to an issue or
pull request. Use synthetic fixtures. Keep personal snapshots outside the
repository.

## Installer changes

Test both autostart modes, reinstalling over an existing version, and behavior
with a damaged or missing carrier. Do not change the Title ID, port or data
paths without a migration plan.
