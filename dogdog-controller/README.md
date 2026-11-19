| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |

# Dogdog controller

## USB keyboard delivery

Keyboard output is serialized across tasks. A key is confirmed internally only
after TinyUSB completes both its key-down and key-release USB transfers. Reports
wait for the endpoint to become ready and use bounded retries for rejected or
failed transfers. Disconnects and missing completion callbacks return failure
within bounded waits.
Webmelden's Tab, time text, and Enter are sent as one sequence that stops at the
first unconfirmed key.

Retries resend the same held-key or released-key state, without inserting a
release between repeated key-down reports. A complete press is never replayed,
because the PC may already have received it. Recovery sends only key-release
reports to avoid a stuck key. Failures are recorded in firmware logs; no delivery
status is shown on the controller display. This confirms USB delivery to the PC
and does not require PC software or check the destination text field.

Host regression checks (PowerShell with LLVM/Clang):

```powershell
./tests/keyboard-regressions/run.ps1
./tests/firmware-crash-regressions/run.ps1
```

Build the firmware from this directory with ESP-IDF 5.5.1 and `idf.py build`.
USB timing, unplug/replug recovery, and keyboard behavior still require board
testing with a PC.

## Original project template

(See the README.md file in the upper level 'examples' directory for more information about examples.)

This is the simplest buildable example. The example is used by command `idf.py create-project`
that copies the project to user specified path and set it's name. For more information follow the [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project)



## How to use example
We encourage the users to use the example as a template for the new projects.
A recommended way is to follow the instructions on a [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project).

## Example folder contents

The project **sample_project** contains one source file in C language [main.c](main/main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt`
files that provide set of directives and instructions describing the project's source files and targets
(executable, library, or both). 

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── main
│   ├── CMakeLists.txt
│   └── main.c
└── README.md                  This is the file you are currently reading
```
Additionally, the sample project contains Makefile and component.mk files, used for the legacy Make based build system. 
They are not used or needed when building with CMake and idf.py.
