Run on Windows with PowerShell and LLVM/Clang:

```powershell
./tests/firmware-crash-regressions/run.ps1
```

This also runs the existing controller regressions. The additional checks compile
production C functions with simulated FreeRTOS queues, clocks, allocation failures,
radio IRQs, and LED driver errors. They cover 10,000 time syncs without heap growth,
10,000 packet sends per queue state, GPIO initialization races in both projects,
LED serialization and lock release after errors, and radio completion/timeout/missing
IRQ behavior across tick wraparound. Sensor startup tests stop before the event loop.
The button check sends 1,000 simulated edges through the queue sizing used at startup.

Generated files go under `dogdog-controller/build/firmware-crash-regressions`.
Firmware builds and device testing remain separate checks.
