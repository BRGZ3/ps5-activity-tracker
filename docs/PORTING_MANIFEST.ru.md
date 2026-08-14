# Playlog: архитектура и переносимые части

[English version](PORTING_MANIFEST.md)

## Архитектура

1. Media PKG устанавливает плитку `ACTV00002`.
2. Плитка открывает `http://127.0.0.1:12888/`.
3. Raw `Playlog.elf` запускает HTTP-сервер и tracker.
4. При первой настройке runtime копирует себя в выбранный автозапуск etaHEN
   или SM+/PLK.
5. Новый PKG доставляет dashboard и ELF через carrier в `icon0.png`.

Пользовательская история хранится только в `/data/ps5-activity` и не должна
попадать в PKG или Git.

## Проверенная схема установки

```text
PKG ACTV00002 -> Media tile + carrier
Playlog.elf  -> однократный запуск через USB/loader
dashboard    -> выбор etaHEN или ShadowMount+/PLK
restart      -> загрузка выбранного автозапуска
```

Raw ELF проверен в целевом сценарии на firmware 4.50. Более широкая таблица
совместимости должна подтверждаться отдельными hardware-тестами.

## Важные границы

- не запускать одновременно ELF и plugin;
- не оставлять два автозапуска Playlog;
- не использовать порт 12888 для другого приложения;
- не копировать `summary.json`, backup и state в source tree;
- carrier проверяет CRC, но не является криптографической подписью.

Ограничения runtime для этого релиза перечислены в
`../KNOWN_LIMITATIONS.ru.md`.
