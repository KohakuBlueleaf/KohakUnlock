# Agent instructions

Read docs/SAFETY.md and docs/AGENT-GUIDE.md before editing. Default tasks are offline source/tests only. Do not load a module, access BARs, change bindings, reboot, modify boot settings or run firmware without explicit authorization and verified recovery access.

- `make test`: CPU-only, firmware-free tests (includes the TU104 synthetic-profile suite).
- `make test-private ASSETS=...`: CPU-only protocol model with private verified inputs.
- `make module`: compile only, execution disabled by default.
- The TU104 RTX 2060 port (kernel/tu104_*, docs/TU104-2060.md) is source-only and unvalidated; regenerate kernel/tu104_profile.h with tools/profile_gen.py from the target card's own PROM diagnostics. Do not hand-edit its hashes.
- Do not weaken guards to make another board run.
- Keep runtime source and model tests synchronized; label simulations vs hardware evidence.
- Never commit local/, assets/, binaries, dumps, full inventories, serials, UUIDs, IPs, credentials or external firmware.
- No automatic public hardware runner in CI.
- Preserve attribution/license texts when adapting upstream code.
- Do not change benchmark methodology while claiming UI-only improvements.
