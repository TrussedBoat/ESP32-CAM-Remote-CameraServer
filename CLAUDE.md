# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

This is a PlatformIO project targeting the ESP32-CAM board (`env:esp32cam`, `platform = espressif32`, `framework = arduino`). It currently contains only the default PlatformIO scaffold in [src/main.cpp](src/main.cpp) — no WiFi or camera logic has been implemented yet despite the repo name.

## Commands

This project uses PlatformIO, not a plain Makefile/CMake toolchain.

- Build: `pio run`
- Build for the (only) env explicitly: `pio run -e esp32cam`
- Upload to the board: `pio run -t upload`
- Serial monitor: `pio device monitor`
- Clean build artifacts: `pio run -t clean`
- Run unit tests (PlatformIO Test Runner, tests live under [test/](test/)): `pio test`
- Run a single test file: `pio test -f <test_name>`
- Static analysis: `pio check`

## Architecture

- [src/main.cpp](src/main.cpp) — single entry point; `setup()` runs once, `loop()` runs repeatedly (standard Arduino structure).
- [include/](include/) — project-wide header files, shared across files in `src/`.
- [lib/](lib/) — private/local libraries; each subdirectory under `lib/` is compiled as its own static library and auto-linked via PlatformIO's Library Dependency Finder (no manual registration needed).
- [test/](test/) — PlatformIO Test Runner unit tests.
- [platformio.ini](platformio.ini) — single environment `esp32cam`; add build flags, upload settings, and library dependencies (`lib_deps`) here as the project grows.
