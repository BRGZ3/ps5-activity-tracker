# Playlog / PS5 Activity Tracker

[English version](README.md)

Локальный трекер игровой активности для взломанной PS5. Playlog работает без
PSN и внешних сервисов: runtime читает системные события, считает игровое и
пауза-время, а dashboard показывает статистику непосредственно на консоли.

Текущий публичный релиз: **1.0.0**.

Публичный tag релиза — `v1.0.0`. В PKG сохранены монотонные внутренние
метаданные установщика (`01.046.000`), чтобы он устанавливался поверх
проверенных версий runtime; не снижайте и не переписывайте это значение при
загрузке asset.

> Это первый публичный релиз v1.0. Перед установкой прочитайте
> [известные ограничения](KNOWN_LIMITATIONS.ru.md).

## Возможности

- PS4 (`CUSA...`) и PS5 (`PPSA...`) игры;
- активное время, Home/пауза, сессии и периоды сегодня/неделя/месяц;
- best-effort названия игр из системных метаданных;
- обратимая отметка игры «Пройдена»;
- CRC-защита основного состояния и предыдущее поколение state;
- backup/restore через dashboard;
- RU/EN dashboard для старого встроенного браузера;
- LAN read-only просмотр с компьютера или телефона;
- offline carrier-обновление через новый PKG.

## Архитектура

```text
Media PKG ACTV00002 -> плитка + dashboard/carrier
Playlog.elf         -> первоначальная настройка и runtime
etaHEN или SM+/PLK  -> автозапуск runtime после перезапуска
HTTP :12888         -> локальный dashboard и API
```

Media PKG не запускает ELF сам и не записывает файлы в `/data`. После
установки PKG один раз запускается `Playlog.elf` через USB/loader, затем в
dashboard выбирается способ автозапуска.

## Установка

1. Скачайте PKG и `Playlog.elf` из GitHub Release `v1.0.0`.
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
/data/ps5_autoloader/autoload.txt
/data/ps5-activity/
```

Выбирайте только один способ автозапуска. Не запускайте одновременно ELF и
старый `.plugin`.

## Dashboard и LAN

На консоли откройте плитку Playlog. Она использует:

```text
http://127.0.0.1:12888/
```

С компьютера или телефона в доверенной локальной сети:

```text
http://<PS5_IP>:12888/
```

LAN-клиенты работают в read-only режиме, но всё равно получают доступ к истории
игр и времени сессий. Не пробрасывайте порт наружу и не открывайте его в
недоверенной сети. В v1.0 у сервера нет аутентификации.

## Данные на консоли

```text
/data/ps5-activity/summary.json
/data/ps5-activity/tracker-state.bin
/data/ps5-activity/tracker-state.prev.bin
/data/ps5-activity/completed-state.bin
/data/ps5-activity/config.json
/data/ps5-activity/backups/
```

История не входит в PKG и не должна попадать в Git. До обновления или теста
сделайте backup через dashboard.

## Обновления

1. Установите новый PKG поверх `ACTV00002`.
2. Откройте dashboard.
3. Нажмите offline update и подтвердите действие.
4. Дождитесь сообщения об automatic backup и применении carrier.
5. Перезапустите runtime, когда dashboard попросит об этом.

Обновление не следует считать защищённым от всех частичных сбоев. Не
обновляйте во время нестабильной работы exploit/runtime и храните отдельную
копию `/data/ps5-activity`.

## Сборка из исходников

Для ELF нужен PS5 Payload SDK и `prospero-clang`:

```bash
cd activity-probe
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk make
```

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

Основной hardware-тест выполнен на firmware 4.50. Поведение raw ELF и
автозапуска на других прошивках/версиях etaHEN или SM+/PLK нужно подтверждать
отдельно. Заполняйте результаты в release notes, не расширяйте матрицу
поддержки по предположению.

## Документы

- [KNOWN_LIMITATIONS.ru.md](KNOWN_LIMITATIONS.ru.md) — известные ограничения;
- [docs/PORTING_MANIFEST.md](docs/PORTING_MANIFEST.md) — архитектура и границы;
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) — проверенная матрица firmware;
- [SECURITY.ru.md](SECURITY.ru.md) — приватность LAN и сообщения об уязвимостях.
