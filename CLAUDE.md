# CLAUDE.md — Real-Time Systems course repo

Zephyr RTOS coursework (labs 01–13). Firmware in C, timing measured on real
hardware with a logic analyzer (PulseView). Environment: WSL2 (Ubuntu), West.

## Protocol rule

- After the initial planning phase, STOP and ask the user to switch to
  Sonnet 5.5 with `/model` before modifying any file or running a build.
  Do not continue until the user confirms the switch.

## Active target

- **Board:** ESP32-S3 DevKitC — `esp32s3_devkitc/esp32s3/procpu`
- **App:** `firmware/kernel` (kernel threads, from Lab 03 onward)
- `firmware/superloop` and the STM32 Nucleo (L476RG, G474RE) / ESP32-C6 targets
  were used in earlier labs only. Do not build or modify them unless asked.

## Build, flash, monitor (ESP32-S3)

```bash
source ~/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null || true
west build -p auto -b esp32s3_devkitc/esp32s3/procpu firmware/kernel
west flash -r esp32
tio $(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n 1) -b 115200
```

- Optional fragments (append after `--`):
  - Potentiometer: `-DEXTRA_DTC_OVERLAY_FILE=pot_esp32.overlay`
  - Display HMI: `-DEXTRA_CONF_FILE=display.conf`
  - `nocache.conf` is STM32-only (ART cache); do not use it on the ESP32-S3.
- Board overlay: `firmware/kernel/boards/esp32s3_devkitc_esp32s3_procpu.overlay`
- Console is 115200 8N1 on the **UART** jack, not the USB jack.
- Pin map and wiring: `firmware/kernel/README.md`.
- **Lab 05 tracing (CTF):** streamed over the USB-Serial-JTAG port (**USB** jack,
  second `/dev/ttyACM*`) via `zephyr,tracing-uart = &usb_serial`. Both USB-C
  ports must be attached, so pick the console tty explicitly instead of
  `head -n 1`. No probe or analyzer needed. Steps: `labs/sops/sop05_tracing.md`
  (Route B, B2–B3). Status and next steps are in Engram topic
  `lab05/tracing-ctf-usb-serial`.

## Hardware limits

- Flash and serial monitor need the board attached to WSL (usbipd). If no
  `/dev/ttyUSB*` / `/dev/ttyACM*` exists, report it instead of retrying.
- Timing results (jitter, latency, C_i, T_i) come from the logic analyzer;
  never invent measurements.

## Repo layout

- `firmware/kernel/` — active app (`src/main.c`, `prj.conf`, overlays)
- `labs/labNN_*.md` — lab reports; status table in `README.md`
- `templates/` — lab, lecture, SOP templates
- `SESSION_FORMAT.md`, `PROJECT_SCENARIO.md`, `BOM.md`, `READINGS.md` — course docs
- `build/` — generated, git-ignored; never edit
