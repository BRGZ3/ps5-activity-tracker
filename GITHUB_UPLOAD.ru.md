# Как опубликовать Playlog v1.0 на GitHub

[English version](GITHUB_UPLOAD.md)

Ниже описана публикация каталога `github-v1.0`. Рабочий каталог с реальными
backup и логами публиковать нельзя.

## 1. Проверить staging-каталог

```bash
cd github-v1.0
python3 tools/check_public_tree.py
python3 -m unittest discover -s tests -v
node --check release-build/build-carrier.js
```

Проверка должна завершиться без найденных private/generated файлов. Если
команда видит IP, summary, state или backup, сначала удалите их из staging.

## Что загружать в GitHub repository

В source repository должен попасть только следующий allowlist:

```text
.github/workflows/ci.yml
.gitignore
KNOWN_LIMITATIONS.md
KNOWN_LIMITATIONS.ru.md
CHANGELOG.md
CHANGELOG.ru.md
CONTRIBUTING.md
CONTRIBUTING.ru.md
GITHUB_UPLOAD.md
GITHUB_UPLOAD.ru.md
LICENSE
RELEASE_AUDIT.ru.md
RELEASE_AUDIT.md
PROJECT_FILES.md
PROJECT_FILES.ru.md
README.md
README.ru.md
SECURITY.md
SECURITY.ru.md
VERSION

activity-probe/
  Makefile
  README.md
  README.ru.md
  config.example.json
  http_server.c
  http_server.h
  lite_main.c
  main.c
  offline_update.c
  offline_update.h
  plugin.mk
  tracker.c
  tracker.h

dashboard/
  index.html

docs/
  COMPATIBILITY.md
  PORTING_MANIFEST.md
  PORTING_MANIFEST.ru.md

release-build/
  Makefile
  README.md
  README.ru.md
  build-carrier.js
  render_icon.js
  app/launcher/index.html
  app/sce_sys/param.json
  builder/PlaylogReleaseBuilder.csproj
  builder/Program.cs

tests/
  autoload_harness.c
  http_server_harness.c
  test_autoload.py
  test_c_tracker.py
  test_dashboard_mobile.py
  test_http_server.py
  tracker_backup_harness.c
  tracker_harness.c
  tracker_midnight_harness.c

tools/
  check_public_tree.py
  make_etahen_plugin.py
```

После `git add .` проверьте список:

```bash
git diff --cached --name-only
```

## Что НЕ загружать в source repository

```text
summary.json
probe-events.jsonl
tracker-state*.bin
completed-state.bin
diagnostics-state.bin
backups.json
config.json
probe.pid
summary.txt

ps5-activity-backup/
ps5 backup/
archive/
.omx/
.deps/
playlog-prerelease-pkg/

activity-probe/*.elf
activity-probe/*.plugin
release-build/app/sce_sys/base-icon.png
release-build/app/sce_sys/icon0.png
release-build/builder/bin/
release-build/builder/obj/
release-build/dist/
release-assets/
*.pkg
*.elf
*.plugin
*.db
*.log
*.jsonl
.DS_Store
```

Не загружайте также локальный PS5 SDK, checkout LibProsperoPKG, реальные
скриншоты dashboard и любые файлы с IP, профилями или историей игр.

## 2. Создать локальный Git-репозиторий

```bash
git init
git branch -M main
git add .
git status --short
git commit -m "Prepare Playlog 1.0.0"
```

До commit проверьте, что в списке нет `*.pkg`, `*.elf`, `summary.json`,
`probe-events.jsonl`, `.omx` и backup. Генерируемые бинарники намеренно
игнорируются.

## 3. Создать пустой репозиторий на GitHub

Создайте новый пустой repository без автоматического README, `.gitignore` и
license, например `playlog-ps5-activity-tracker`. Затем добавьте remote:

```bash
git remote add origin git@github.com:<USER>/<REPOSITORY>.git
git push -u origin main
```

HTTPS вместо SSH также подходит:

```bash
git remote add origin https://github.com/<USER>/<REPOSITORY>.git
git push -u origin main
```

## 4. Подготовить артефакты v1.0

Если доступны PS5 Payload SDK и LibProsperoPKG, из корня v1.0 выполните:

```bash
cd activity-probe
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk make
cd ../release-build
make
```

В результате появятся `release-build/dist/Playlog.elf` и PKG. Перед публикацией
убедитесь, что C# validator напечатал `Accepted: True`.

Если сборка выполняется на машине сопровождающего, можно использовать уже
проверенные бинарники из текущего release candidate, но после каждой смены
dashboard/runtime их нужно пересобрать.

Посчитать контрольные суммы:

```bash
mkdir -p release-assets/v1.0.0
cp release-build/dist/Playlog.elf release-assets/v1.0.0/
cp release-build/dist/*.pkg release-assets/v1.0.0/
(cd release-assets/v1.0.0 && shasum -a 256 Playlog.elf *.pkg > SHA256SUMS.txt)
```

`release-assets/` полностью исключён из Git. Это только локальная папка для
подготовки файлов перед загрузкой в GitHub Release.

Публичный tag — `v1.0.0`; имя и metadata PKG остаются `A0146` (`01.046.000`)
для монотонной совместимости установки на консоли. Не переименовывайте PKG и
не снижайте его внутреннюю версию при публикации.

## 5. Создать GitHub Release

В GitHub:

1. откройте **Releases → Draft a new release**;
2. создайте tag `v1.0.0` от ветки `main`;
3. заголовок: `Playlog 1.0.0`;
4. приложите PKG, `Playlog.elf` и `SHA256SUMS.txt`;
5. вставьте разделы `CHANGELOG.md` и `KNOWN_LIMITATIONS.ru.md` в описание;
6. опубликуйте обычный релиз.

После скачивания трёх assets сложите их в одну папку и проверьте командой
`shasum -a 256 -c SHA256SUMS.txt`.

Для CLI GitHub можно использовать:

```bash
gh release create v1.0.0 \
  release-assets/v1.0.0/*.pkg \
  release-assets/v1.0.0/Playlog.elf \
  release-assets/v1.0.0/SHA256SUMS.txt \
  --title "Playlog 1.0.0" \
  --notes-file KNOWN_LIMITATIONS.ru.md
```

## 6. Проверить опубликованный релиз

С чистого checkout проверьте:

```bash
git clone https://github.com/<USER>/<REPOSITORY>.git verify-playlog
cd verify-playlog
python3 tools/check_public_tree.py
python3 -m unittest discover -s tests -v
```

Затем на отдельной консоли или после сохранения backup проверьте установку
PKG, однократный запуск ELF, выбор режима автозапуска, открытие плитки и
обновление поверх предыдущего релиза.

## 7. Как выпускать следующую версию

1. изменить `VERSION`;
2. обновить `CHANGELOG.md` и таблицу совместимости;
3. пересобрать runtime и PKG;
4. проверить `Accepted: True`, тесты и SHA256;
5. создать новый tag, например `v1.0.1`;
6. загрузить новые PKG/ELF как assets, не коммитить их в source tree.
