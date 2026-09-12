# Configuration v2 integrated host tests

Run from the repository root on the configured Windows host:

```powershell
./app/Tools/test_configuration_v2.ps1
```

The runner configures GCC/Ninja Debug, builds production C code with boundary adapters,
then runs all CTest suites. Logs and JUnit output are in `artifacts/configuration_v2/host-tests/`.
Tool paths can be overridden with `-Cmake`, `-Ninja`, and `-Compiler`.
The pinned Unity dependency is the only fetched source; Middlewares remains read-only.

| Directory | Production behavior exercised |
|---|---|
| common | Real model, codec, Mbed TLS and LwIP; independent 48/8475-byte fixtures |
| configuration_codec | Schema, lengths, characters, sentinels, padding, CA and utilization |
| configuration_service | NOR boundary simulation, three sectors, CRC, commit and recovery |
| management | Real framing/Transport/Service, USB/RTOS adapters, Python tools |
| mqtt | Real publisher/vendor MQTT, independent CONNECT oracle and real TLS policy setup |

AddressSanitizer and UndefinedBehaviorSanitizer require a supported Linux compiler.
On this Windows host, run the LF-preserved runner through WSL:

```powershell
wsl -d Ubuntu-24.04 --exec bash /mnt/c/Users/tianf/.codex/worktrees/f756/modbus-gateway-stm32/app/Tools/test_configuration_v2_sanitizers.sh
```

For another checkout use its Linux path. The shell runner resolves the repository from its own
location, uses a separate `artifacts/configuration_v2/host-tests-asan/` build, and reuses the
native build's Unity source when present. It does not change vendor files.

Firmware is built separately with `app/Tools/build_configuration_v2.ps1 -Phase integrated`.
That runner rebuilds both Keil targets and records their independent outputs, map regions,
DMA buffer addresses and SHA-256 hashes. Use `-Targets app_A` or `-Targets app_B` for one target.
It performs no flashing, device writes, reset, serial connection or broker connection.

Results, commit provenance, resource budgets and hardware limitations are recorded in
`app/Spec/implementation_reports/configuration_v2_integration.md`. Host adapters are not evidence
of real USB/DMA operation, electrical power-cut recovery, TLS handshakes, heap peaks or stack watermarks.
