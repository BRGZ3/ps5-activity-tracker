# Playlog v1.0: architecture and portable parts

[Русская версия](PORTING_MANIFEST.ru.md)

## Architecture

1. The Media PKG installs the `ACTV00002` tile.
2. The tile opens `http://127.0.0.1:12888/`.
3. `Playlog.elf` serves the HTTP API and tracker.
4. First-run setup copies the runtime to the selected etaHEN or SM+/PLK
   autostart location.
5. A newer PKG delivers dashboard and ELF through the `icon0.png` carrier.

User history lives only under `/data/ps5-activity` and must not be included in
the PKG or Git.

## Tested v1.0 installation flow

```text
PKG ACTV00002 -> Media tile + carrier
Playlog.elf  -> one-time USB/loader launch
dashboard    -> select etaHEN or ShadowMount+/PLK
restart      -> selected autostart loads the runtime
```

The raw ELF was tested in the target scenario on firmware 4.50. A wider
compatibility matrix requires separate hardware tests.

## Important boundaries

- do not run ELF and plugin together;
- do not leave two Playlog autostarts enabled;
- do not use port 12888 for another application;
- do not copy summary, backups or state into the source tree;
- the carrier detects corruption with CRC but is not a cryptographic signature.

See `../KNOWN_LIMITATIONS.md` for the runtime limitations that apply to this
release.
