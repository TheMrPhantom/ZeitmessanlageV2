Run the host regressions on Windows with PowerShell and LLVM/Clang installed:

```powershell
./tests/controller-regressions/run.ps1
```

The runner compiles the production C functions against simulated queues, clocks,
allocation failures, and task notifications. It checks packet bounds and unaligned
payloads, sensor-status ownership when queues are full, ACK/timeout ordering, startup
interrupt ordering, 64-bit display values, clock jumps, restarts, and stale final times.
Generated C files and executables go under `dogdog-controller/build/controller-regressions`.
Hardware timing and physical radio behavior still need device testing.
