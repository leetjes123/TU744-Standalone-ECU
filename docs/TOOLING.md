# Artifact identity and packaging

The build and packaging checks are development checks, not production approval.
They use explicit failures instead of Python `assert`, so `python -O` cannot
remove checks from `build.py`, `verify_artifacts.py`, `artifacts.py` or
`package_tunerpro.py`. Older emulator tests still require ordinary Python.

Each Keil profile now produces a schema-2 manifest, including the default
output-inhibited `c166` build. It records the complete firmware source inventory,
build inputs, compiler/assembler/linker/HEX-converter and runtime-library hashes,
and the resulting HEX, map, link command, build log and optional binary hashes.
The build checks for changed source/build/toolchain inputs during compilation.
It removes the previous manifest before rebuilding; failed builds cannot reuse
that manifest. Old build directories must be rebuilt for the new verifier.

Verification checks the exact input inventory, artifact identities, unique IRQ
allocation and default output gate. The shared HEX reader rejects bad checksums,
lengths, control records, unknown record types, overlap, absent reset/EOF records,
trailing records and forbidden flash ranges. An experimental binary must equal
the image reconstructed from the linked HEX, including erased reserved regions.
This is a host artifact check; it does not check flash contents on an ECU.

```powershell
python firmware/tu5jp_standalone/tools/verify_artifacts.py
python -O firmware/tu5jp_standalone/tools/verify_artifacts.py --profile stock-95080
python firmware/tu5jp_standalone/tools/verify_artifacts.py --profile engine-experimental
```

Original standalone/Wizard source isolation remains checked by default. A moved
checkout can use `--legacy-root` and `--wizard-root`. A checkout without those
private sources can explicitly use `--skip-legacy`; the output and JSON then
say `not_checked`. This does not establish original-source isolation.
`project.py` now regenerates source groups from the committed uVision project
instead of requiring the private original project. An installed licensed Keil
toolchain, native compiler and locally supplied TunerPro SDK are still needed;
an independent clean-room build has not been demonstrated.

The plugin build creates its own manifest only after the parser/DLL tests pass.
It records plugin/test/firmware input hashes, SDK hashes, native compiler/linker/
resource-compiler identities, DLL identity and test-log identity. Packaging
requires both current firmware and plugin manifests. It rejects source drift,
DLL substitution, missing definitions and mismatched firmware. The ZIP includes
both manifests, an explicit experimental `PACKAGE.json` and a hash inventory.
It verifies a temporary archive before atomically replacing the output file.
No SDK, base map or calibration is included.

Hashes provide reproducible identity and drift detection, not signatures or an
access-control policy. Authoritative schema generation, controlled build-tool
distribution, signed release/access policy and independent portability remain
G07 work. Engine-neutral configuration and physical validation remain separate
gates. `production_release_accepted` remains false in verification/package output.

Negative regressions run without hardware or serial I/O:

```powershell
python firmware/tu5jp_standalone/tests/test_artifacts.py
python -O firmware/tu5jp_standalone/tests/test_artifacts.py
```
