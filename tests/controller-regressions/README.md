Run the host regressions on Windows with PowerShell and LLVM/Clang installed:

```powershell
./tests/controller-regressions/run.ps1
```

The runner compiles the production C functions against simulated queues, clocks,
allocation failures, and task notifications. It checks packet bounds and unaligned
payloads, sensor-status ownership when queues are full, ACK/timeout ordering, startup
interrupt ordering, 64-bit display values, clock jumps, restarts, and stale final times.
Serial regressions run the production receiver task with simulated UART input.
They model a burst arriving while the task is waiting: without the driver's
receive buffer, input beyond the ESP32-S3's 128-byte FIFO is lost. They check a
175-byte line, twelve consecutive commands, reuse of an installed UART driver,
recovery after transient EOF, and recovery after an oversized line without
executing a command in its tail.
Generated C files and executables go under `dogdog-controller/build/controller-regressions`.
Hardware timing and physical radio behavior still need device testing.
