# Playlog runtime

[English version](README.md)

`activity-probe` — native runtime для PS5. Он читает системные события,
сопоставляет App ID с `CUSA...`/`PPSA...`, считает сессии и обслуживает
локальный dashboard на порту `12888`.

## Сборка

```bash
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk make
```

Основной результат — `activity-probe.elf`. Файлы `.elf` и `.plugin` являются
generated artifacts и исключены из Git.

## Однократный запуск

Убедитесь, что loader принимает payload на порту `9021`, затем выполните:

```bash
cat activity-probe.elf | nc <PS5_IP> 9021
```

После запуска откройте Media-плитку Playlog и завершите первичную настройку.
Runtime ждёт пять секунд до установки выбранного автозапуска.

## Автозапуск

Для etaHEN:

```text
/data/etaHEN/plugins/Playlog.elf
/data/etaHEN/plugins/Playlog.elf.auto_start
```

Для ShadowMount+/PLK:

```text
/data/ps5_autoloader/Playlog.elf
/data/ps5_autoloader/autoload.txt
```

В релизе выбирается только один режим. Не оставляйте одновременно старый
`ps5-activity-tracker.plugin`, raw ELF и строку SM+ autoload.

## Данные

```text
/data/ps5-activity/summary.json
/data/ps5-activity/tracker-state.bin
/data/ps5-activity/tracker-state.prev.bin
/data/ps5-activity/completed-state.bin
/data/ps5-activity/config.json
/data/ps5-activity/backups/
```

Путь `probe-events.jsonl` может присутствовать и растёт без ротации.
Не переносите его в публичный issue вместе с пользовательской историей.

## Ограничения

- основной hardware-тест: firmware 4.50;
- PS5 portable-игры не всегда дают явное событие exit;
- названия игр определяются best effort;
- timezone до настройки использует fallback `Europe/Moscow`;
- процесс и режимы автозапуска требуют взаимоисключения вручную;
- v1.0 не обещает rollback при каждом сценарии частичного обновления.

Известные ограничения runtime: [../KNOWN_LIMITATIONS.ru.md](../KNOWN_LIMITATIONS.ru.md).
