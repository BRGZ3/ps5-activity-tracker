# Playlog / PS5 Activity Tracker

[English version](README.md)

Локальный трекер игровой активности для взломанной PS5. Playlog работает без
PSN и внешних сервисов: runtime читает системные события, считает игровое и
пауза-время, а dashboard показывает статистику непосредственно на консоли.

Текущий стабильный релиз: **1.3.0**.

В PKG сохранены монотонные внутренние метаданные установщика (`01.070.000`),
чтобы он устанавливался поверх
проверенных версий runtime; не снижайте и не переписывайте это значение при
загрузке asset.

> Перед установкой прочитайте
> [известные ограничения](KNOWN_LIMITATIONS.ru.md).

## Возможности

- PS4 (`CUSA...`) и PS5 (`PPSA...`) игры;
- активное время, Home/пауза, сессии и периоды сегодня/неделя/месяц;
- best-effort названия и локальные обложки игр из системных метаданных;
- обратимая отметка игры «Пройдена»;
- CRC-защита основного состояния и предыдущее поколение state;
- backup/restore через dashboard;
- RU/EN dashboard для старого встроенного браузера;
- LAN read-only просмотр с компьютера или телефона;
- отдельная библиотека игр: сначала игры, установленные сейчас, ниже —
  записи системного реестра об играх, которых больше нет в файловой системе;
- offline carrier-обновление через новый PKG.

## Архитектура

```text
Media PKG ACTV00002 -> плитка + dashboard/carrier
Playlog.elf         -> первоначальная настройка и runtime
etaHEN или SM+/PLK  -> автозапуск runtime после перезапуска
HTTP :12888         -> локальный dashboard и API
```

Основная страница остаётся прежним экраном статистики. Отдельная страница
библиотеки обслуживается тем же runtime и читает текущий реестр игр из
`app.db`. В неё входят строки CUSA/PPSA независимо от зависящего от прошивки
`categoryType`; известные медиа/системные приложения и DLC исключены. Если
реестр временно недоступен, выполняется fallback-сканирование поддерживаемых
внутренних, внешних и portable-каталогов. Страница не изменяет state трекера
или вёрстку основной страницы.

Media PKG не запускает ELF сам и не записывает файлы в `/data`. После
установки PKG один раз запускается `Playlog.elf` через USB/loader, затем в
dashboard выбирается способ автозапуска.

## Установка

1. Скачайте PKG и `Playlog.elf` из GitHub Release.
2. Установите PKG с Title ID `ACTV00002`.
3. Один раз запустите `Playlog.elf` через USB/etaHEN Toolbox или loader на
   порт `9021`. Runtime ждёт пять секунд перед инициализацией.
4. Откройте плитку Playlog на консоли.
5. Выберите `etaHEN` или `ShadowMount+ / PLK`.
6. Перезапустите выбранный runtime или консоль.

Создаваемые пути:

```text
/data/etaHEN/plugins/Playlog.elf
/data/etaHEN/plugins/Playlog.elf.auto_start
/data/ps5_autoloader/Playlog.elf
/data/ps5_autoloader/autoload.txt (изменяется только при существующей цепочке)
/data/ps5-activity/
```

Выбирайте только один способ автозапуска. Не запускайте одновременно ELF и
старый `.plugin`.

В режиме `ShadowMount+ / PLK` Playlog никогда не создаёт `autoload.txt`. Если
файл отсутствует, пуст или содержит только комментарии, задержки и директивы,
он остаётся без изменений, а страница настройки выводит инструкцию для ручной
настройки. Это сохраняет встроенный fallback на Payload Manager в цепочках
BD-JB, Y2JB, Lua и Unified Autoloader.

Если существующий `autoload.txt` уже содержит другой payload, Playlog добавляет
`!5000` и `Playlog.elf` в конец. Если Playlog уже прописан, файл и выбранный
пользователем порядок запуска остаются без изменений. Файл только с Playlog
также сохраняется, но отмечается как требующий ручной настройки. Пользователям
Payload Manager следует включить Playlog в автозагрузке самого PLK.

## Dashboard и LAN

На консоли откройте плитку Playlog. Она использует:

```text
http://127.0.0.1:12888/
```

Нажмите значок библиотеки в шапке или откройте `/library.html`: сначала будут
показаны игры, реально присутствующие в файловой системе, ниже — старые записи
системного реестра. После удаления игры строка может остаться в
`/system_data/priv/mms/app.db`, поэтому runtime проверяет content-root перед
установкой флага `installed`. Постоянный кэш `appmeta` не считается
доказательством установки; учитываются mount-ссылки ShadowMount+ в
`/user/app/<TITLE_ID>`. Этот каталог не
зависит от активности Playlog и поэтому показывает игры, которыми пользовались
до установки трекера. DLC из `addcont.db` пока намеренно не выводится.

Оставшийся после удаления образа staging-каталог `/user/app/<TITLE_ID>`, в
котором есть только `sce_sys/param.sfo` или `param.json`, также считается
удалённым: одних метаданных недостаточно. Для установленной игры должен быть
`eboot.bin`/`app.pkg` либо `mount.lnk` с существующей целью.

Если библиотека пуста, API возвращает `source`, `database_status` и
`database_rows`. По ним в диагностическом отчёте можно отличить пустой реестр
от заблокированной или недоступной базы данных.

С компьютера или телефона в доверенной локальной сети:

```text
http://<PS5_IP>:12888/
```

LAN-клиенты работают в read-only режиме, но всё равно получают доступ к истории
игр и времени сессий. Не пробрасывайте порт наружу и не открывайте его в
недоверенной сети. У текущего сервера нет аутентификации.

## Данные на консоли

```text
/data/ps5-activity/summary.json
/data/ps5-activity/tracker-state.bin
/data/ps5-activity/tracker-state.prev.bin
/data/ps5-activity/completed-state.bin
/data/ps5-activity/config.json
/data/ps5-activity/backups/
/data/ps5-activity/covers/
```

История не входит в PKG и не должна попадать в Git. До обновления или теста
сделайте backup через dashboard.

## Обновления

1. Установите новый PKG поверх `ACTV00002`.
2. Откройте dashboard.
3. Нажмите offline update и подтвердите действие.
4. Дождитесь сообщения об automatic backup и применении carrier.
5. Перезапустите runtime, когда dashboard попросит об этом.

Перед применением обновления dashboard автоматически создаёт backup. Updater
обновляет все найденные runtime-копии etaHEN, PLK/ShadowMount+ и USB-autoloader
и только после успешной записи runtime отмечает carrier применённым. Для
защиты от выключения питания или нестабильной работы exploit/runtime храните
отдельную копию `/data/ps5-activity`.

## Сборка из исходников

Для ELF нужны PS5 Payload SDK, `prospero-clang` и статический пакет
`ps5-payload-sqlite` (он устанавливает `sqlite3.h` и `libsqlite3.a` в
`/user/homebrew`):

```bash
cd activity-probe
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk make
```

Перед этой командой соберите/установите пакет SQLite из репозитория PS5
Payload pacbrew в sysroot SDK. Если пакет находится в другом месте, задайте
`SQLITE_PREFIX`.

Для Media PKG нужен LibProsperoPKG и Docker ARM64:

```bash
cd release-build
make
```

Сборщик должен завершиться с `Accepted: True`. Generated ELF/PKG не коммитятся
в source tree, а загружаются как GitHub Release Assets.

## Проверка

```bash
python3 -m unittest discover -s tests -v
python3 tools/check_public_tree.py
node --check release-build/build-carrier.js
```

## Совместимость

Основной hardware-тест выполнен на firmware 4.50. Учёт foreground-сессий через
`SceShellCoreUtilAppFocus` также проверен пользователем на firmware 11.60 с
ShadowMount+/PLK. Остальные сочетания runtime и firmware нужно подтверждать
отдельно.

## Документы

- [KNOWN_LIMITATIONS.ru.md](KNOWN_LIMITATIONS.ru.md) — известные ограничения;
- [docs/PORTING_MANIFEST.md](docs/PORTING_MANIFEST.md) — архитектура и границы;
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) — проверенная матрица firmware;
- [SECURITY.ru.md](SECURITY.ru.md) — приватность LAN и сообщения об уязвимостях.
