Run on Windows with PowerShell and LLVM/Clang installed:

```powershell
./tests/keyboard-regressions/run.ps1
```

The runner compiles the complete production `dogdog-controller` keyboard driver
against simulated FreeRTOS synchronization and USB host transfers. It checks
completion of key-down and release, endpoint readiness and rejected submissions,
transfer failures, missing and invalid callbacks, disconnects, stale completions,
release recovery, and serialized text output. Generated executables go under
`dogdog-controller/build/keyboard-regressions`.

Recovery checks run the background task's release-only routine under its mutex
after key-release retries are exhausted, and verify that it preserves the failed
operation's status. Time-entry checks ensure a failed prefix or partial time
cannot send the final Enter.

These deterministic host checks do not replace testing USB enumeration and
physical button timing with the controller attached to a PC.
