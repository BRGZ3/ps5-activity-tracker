# Playlog runtime

[English version](README.md)

`activity-probe` — native runtime для PS5. Он читает системные события,
сопоставляет App ID с `CUSA...`/`PPSA...`, считает сессии и обслуживает
локальный dashboard на порту `12888`.

Страница библиотеки доступна по `/library.html`, её read-only API —
`/api/installed-games`. Сканер читает системный реестр игр
`/system_data/priv/mms/app.db` и принимает корректные строки CUSA/PPSA
независимо от зависящего от прошивки `categoryType`. Для каждой строки
дополнительно проверяются content-root внутренних, внешних и portable-установок.
Постоянный кэш `appmeta` не считается достаточным доказательством, потому что
он может сохраниться после удаления игры; учитываются mount-ссылки ShadowMount+
в `/user/app/<TITLE_ID>`.
API возвращает `installed:true` для реально присутствующих игр и
`installed:false` для ранее зарегистрированных, но отсутствующих сейчас.
Известные медиа/системные приложения и DLC (`addcont.db`) не включаются. Если
реестр временно недоступен, выполняется fallback-сканирование внутренних,
внешних и portable-каталогов.

Оставшийся staging-каталог `/user/app/<TITLE_ID>` только с
`sce_sys/param.sfo` или `param.json` не считается живым content-root. Нужен
`eboot.bin`/`app.pkg` либо `mount.lnk` с существующей целью.

Если игры не найдены, API дополнительно возвращает `source`,
`database_status` и `database_rows`. По этим полям можно отличить пустой
реестр от недоступной или заблокированной БД при диагностике на консоли.

## Сборка

```bash
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk make
```

В SDK также должен быть статический пакет `ps5-payload-sqlite` в
`/user/homebrew` (`sqlite3.h` и `libsqlite3.a`). Для другого расположения
задайте `SQLITE_PREFIX`.

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
/data/ps5-activity/covers/
/data/ps5-activity/backups/
```

Общее время, сессии, отметка прохождения, название и сохранённая обложка
остаются в истории Playlog после удаления игры с консоли. Второй блок
библиотеки не зависит от этой истории и показывает системный реестр.

Playlog также отслеживает PID `SceShellUI`. Если оболочка перезапустилась, а
runtime продолжает работать, устаревший AppFocus handle закрывается и
открывается заново. Runtime не подписывается на события перехода питания и не
останавливает себя во время suspend/shutdown: жизненным циклом управляет сама
консоль, поэтому payload не удерживает систему на синем огне.

Чтение `/dev/klog` самовосстанавливается: временные EOF/ошибки чтения закрывают
и переоткрывают устройство в фоне, а не завершают runtime. HTTP-ответы
отправляются с `MSG_NOSIGNAL`, поэтому отключившийся браузер не может убить
Playlog через `SIGPIPE`. Если сам listener завершился, основной цикл повторно
поднимает LAN-сервер.

Обложки сначала ищутся в обычных appmeta/mounted-content путях, затем в
read-only полях `icon0Info`/`metaDataPath` базы `app.db` (включая суффикс
`?ts=...` от оболочки). Копия сохраняется в
`/data/ps5-activity/covers`.

Путь `probe-events.jsonl` может присутствовать и растёт без ротации.
Не переносите его в публичный issue вместе с пользовательской историей.

## Ограничения

- основной hardware-тест: firmware 4.50;
- PS5 portable-игры не всегда дают явное событие exit;
- названия игр определяются best effort;
- timezone до настройки использует fallback `Europe/Moscow`;
- процесс и режимы автозапуска требуют взаимоисключения вручную;
- обновление не обещает rollback при каждом отключении питания или partial write.

Известные ограничения runtime: [../KNOWN_LIMITATIONS.ru.md](../KNOWN_LIMITATIONS.ru.md).
