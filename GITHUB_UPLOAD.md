# How to publish Playlog v1.0 on GitHub

[Русская версия](GITHUB_UPLOAD.ru.md)

This procedure publishes the `github-v1.0` directory. Never publish the
working directory that contains real backups or console logs.

## 1. Check the staging directory

```bash
cd github-v1.0
python3 tools/check_public_tree.py
python3 -m unittest discover -s tests -v
node --check release-build/build-carrier.js
```

The public-tree check must finish without private or generated source files. If
it finds an IP, summary, state or backup, remove it from staging first.

## What to upload to the GitHub source repository

The source repository should contain exactly this allowlist:

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

After `git add .`, verify the staged list:

```bash
git diff --cached --name-only
```

## What NOT to upload to the source repository

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

Do not upload the local PS5 SDK, the LibProsperoPKG checkout, real dashboard
screenshots, or files containing IP addresses, profile names or game history.

## 2. Create the local Git repository

```bash
git init
git branch -M main
git add .
git status --short
git commit -m "Prepare Playlog 1.0.0 repository"
```

Before committing, make sure there are no `*.pkg`, `*.elf`, `summary.json`,
`probe-events.jsonl`, `.omx` or backup files in the staged list. Generated
binaries are intentionally ignored.

## 3. Create an empty GitHub repository

Create an empty repository without an automatic README, `.gitignore` or
license, for example `playlog-ps5-activity-tracker`. Then add its remote:

```bash
git remote add origin git@github.com:<USER>/<REPOSITORY>.git
git push -u origin main
```

HTTPS is also fine:

```bash
git remote add origin https://github.com/<USER>/<REPOSITORY>.git
git push -u origin main
```

## 4. Build v1.0 artifacts

If the PS5 Payload SDK and LibProsperoPKG are available:

```bash
cd activity-probe
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk make
cd ../release-build
make
```

The C# validator must print `Accepted: True`.

If the release is built on the maintainer machine, the already verified
candidate binaries can be used, but rebuild them after every dashboard or
runtime change.

Calculate checksums:

```bash
mkdir -p release-assets/v1.0.0
cp release-build/dist/Playlog.elf release-assets/v1.0.0/
cp release-build/dist/*.pkg release-assets/v1.0.0/
(cd release-assets/v1.0.0 && shasum -a 256 Playlog.elf *.pkg > SHA256SUMS.txt)
```

`release-assets/` is completely ignored by Git. It is only a local staging
directory for files that will be uploaded to a GitHub Release.

The public tag is `v1.0.0`; the package filename and metadata remain `A0146`
(`01.046.000`) for monotonic console installation compatibility. Do not rename
or lower the package version while publishing this release.

## 5. Create the GitHub Release

In GitHub:

1. open **Releases → Draft a new release**;
2. create tag `v1.0.0` from `main`;
3. set the title to `Playlog 1.0.0`;
4. attach the PKG, `Playlog.elf` and `SHA256SUMS.txt`;
5. include the `CHANGELOG.md` and `KNOWN_LIMITATIONS.md` sections in the
   description;
6. publish it as a normal release.

After downloading the three assets, verify them in one directory with
`shasum -a 256 -c SHA256SUMS.txt`.

GitHub CLI alternative:

```bash
gh release create v1.0.0 \
  release-assets/v1.0.0/*.pkg \
  release-assets/v1.0.0/Playlog.elf \
  release-assets/v1.0.0/SHA256SUMS.txt \
  --title "Playlog 1.0.0" \
  --notes-file KNOWN_LIMITATIONS.md
```

## 6. Verify the published release

From a clean checkout:

```bash
git clone https://github.com/<USER>/<REPOSITORY>.git verify-playlog
cd verify-playlog
python3 tools/check_public_tree.py
python3 -m unittest discover -s tests -v
```

Then, on a separate console or after saving a backup, test PKG installation,
one-time ELF launch, mode selection, tile opening and an update over the
previous release.

## 7. Publish the next release

1. update `VERSION`;
2. update `CHANGELOG.md` and the compatibility table;
3. rebuild runtime and PKG;
4. verify `Accepted: True`, tests and SHA256;
5. create a new tag such as `v1.0.1`;
6. upload new PKG/ELF files as assets, not as source files.
