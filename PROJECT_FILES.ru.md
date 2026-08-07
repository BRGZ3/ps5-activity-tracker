# Карта репозитория v1.0

[English version](PROJECT_FILES.md)

```text
activity-probe/       C runtime, tracker, HTTP API и offline carrier updater
dashboard/            автономный HTML/CSS/JS dashboard без внешних CDN
release-build/        Media PKG layout, carrier builder и C# package validator
tests/                 host-тесты tracker, backup, HTTP, mobile UI и autoload
tools/                 небольшие build/release-проверки
docs/                  архитектурные и porting-заметки
release-assets/        локальное место для PKG/ELF перед загрузкой в GitHub Release
```

Персональные данные консоли не являются частью репозитория. Runtime создаёт их
на самой PS5 под `/data/ps5-activity/`:

```text
summary.json
tracker-state.bin
tracker-state.prev.bin
completed-state.bin
config.json
backups/
```

PKG не должен содержать эти файлы. `release-assets/` игнорируется Git и служит
только локальным staging-местом для файлов, которые затем загружаются как
GitHub Release Assets.
