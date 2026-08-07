# v1.0 repository map

[Русская версия](PROJECT_FILES.ru.md)

```text
activity-probe/       C runtime, tracker, HTTP API and offline carrier updater
dashboard/            standalone HTML/CSS/JS dashboard with no external CDN
release-build/        Media PKG layout, carrier builder and C# validator
tests/                 host tests for tracker, backup, HTTP, mobile UI and autoload
tools/                 small build/release helpers
docs/                  architecture and porting notes
release-assets/        local place for PKG/ELF before GitHub Release upload
```

Personal console data is not part of the repository. The runtime creates it on
the PS5 under `/data/ps5-activity/`:

```text
summary.json
tracker-state.bin
tracker-state.prev.bin
completed-state.bin
config.json
backups/
```

The PKG must not contain these files. `release-assets/` is ignored by Git and
exists only as local staging for files that will be uploaded as GitHub Release
Assets.
