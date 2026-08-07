# Playlog v1.0 release audit

[Русская версия](RELEASE_AUDIT.ru.md)

Audit date: 2026-08-06

Status: `PUBLIC v1.0 / KNOWN LIMITATIONS`

The dashboard/runtime happy path works in the target PS5 4.50 scenario. This
v1.0 release is published with known runtime lifecycle, update, LAN privacy
and build-reproducibility limitations. This document keeps those decisions
visible while rapid maintenance updates address them.

## P0 — hardening priorities

### 1. Clean public tree

Real `summary.json`, `probe-events.jsonl`, `tracker-state*.bin`, backups,
console IPs, game history, `.omx` logs and intermediate build directories must
not be published. The public repository keeps only source, tests and
documentation. PKG/ELF files belong in GitHub Release Assets, not in source
history.

### 2. Runtime and mode exclusivity

The current PID file is not a real lock: a second process can overwrite it and
write the same files. Switching between etaHEN and SM+/PLK can also leave the
opposite autostart entry. That can produce two processes, a port conflict and
corrupted history.

Implement an atomic process lock, confirm termination of the old
instance, and remove the opposite autostart as one transaction.

### 3. Transactional update and restore

Dashboard and ELF are currently updated file by file. A failure after the first
replacement can leave mixed versions, while the pre-update backup contains user
state but not the previous software files. Restore also replaces sidecars
sequentially.

Use staging, verify the complete set, keep a rollback journal, write the commit
marker only after verification, and retain the previous working runtime and
dashboard.

### 4. Make persistence failures visible

State-write failures are currently ignored before a backup is created. Return
the status of `write_state`/`write_summary`, do not report a backup as current
when the on-disk state is stale, and expose degraded state in the dashboard.

### 5. Make the production carrier unambiguous

Production code must not automatically select old `ACTV00003`, `ACTV00005` or
`ACTV00006` carriers. Production should use only `ACTV00002`; migration from
old test builds should be an explicit path. Add downgrade prevention and target
product/schema checks.

### 6. LAN privacy

Read-only blocks remote POST requests, but it does not protect history from
being viewed. The server listens on LAN without authentication, and wildcard
CORS makes cross-site reads easier.

The next hardening update should implement at least one of:

1. loopback by default plus an explicit LAN toggle;
2. a random token for LAN reads;
3. a token/nonce in a custom header for every mutating operation.

In all cases, never expose port `12888` to the Internet.

## P1 — rapid-update backlog

- Make builds reproducible: runtime must be rebuilt from source rather than
  taken from an arbitrary old `.elf`; pin or clearly document LibProsperoPKG
  and the Docker image.
- Add CI for host tests, public-tree checks, JavaScript syntax and version
  consistency.
- Synchronize versions across README, dashboard footer, runtime, carrier and
  PKG metadata.
- Replace the old `activity-probe/README.md` instructions for probe v0.4.0 and
  plugin v1.30, and remove commands for the missing root `ps5_tracker.py`.
- Publish a compatibility table containing firmware, etaHEN/SM+/PLK version,
  launch method, date and result. Do not claim more support than was tested.
- Keep `LICENSE`, `SECURITY.md`, `CONTRIBUTING.md` and `CHANGELOG.md` current.

## P2 — later maintenance

- rotate the unbounded `probe-events.jsonl`;
- implement real system timezone/DST support instead of the Moscow fallback;
- add user profiles;
- export CSV/JSON;
- classify diagnostic errors;
- add a dedicated diagnostics tab;
- add a more general binary-state migration layer.

## Future hardening gate

For a hardened future release, clean installation and update must pass in both
modes, concurrent starts must be impossible, partial update/restore must roll
back, LAN access must have an explicit trust policy, and
`python3 -m unittest discover -s tests -v` must work from a clean clone without
extra `PYTHONPATH` settings.
