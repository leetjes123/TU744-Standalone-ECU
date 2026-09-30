# Restored knock implementation plan

`IMPLEMENTATION-PLAN.md` was copied from the user-supplied reverse-engineering
workspace, `docs/audits/tu744-knock-2026-09-30/IMPLEMENTATION-PLAN.md`. Its original
planning status and relative evidence references are preserved. Resolve those
original evidence paths within that workspace, configured through `TU744_OEM_REPO`.

The user subsequently approved a smaller tuning interface: OEM-seeded gain,
filter bands, window/threshold/load curves, scalar retard step/ceiling and a
recovery-speed multiplier. Internal tracking/diagnostic settings stay off the
tuning page. Current implementation and validation status are documented in
[KNOCK.md](../../KNOCK.md) and [0.0.2 notes](../../RELEASE-0.0.2.md).
