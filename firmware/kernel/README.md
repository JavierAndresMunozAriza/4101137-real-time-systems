# firmware/superloop — the provided superloop

Reference Zephyr app that students **run, read, and measure** in week 2 — their
Zephyr *get started* — and that module 2 migrates, guided, to kernel threads.

One `main` loop + ISRs, no scheduler. Each task raises its instrumentation GPIO
on entry and drops it on exit, so the logic analyzer sees exactly who ran and for
how long.

| Task | Type | C0116-DK | Nucleo-L476RG | ESP32-S3 | ESP32-C6 |
|---|---|---|---|---|---|
| Sampling (1 kHz: pressure ADC, e-stop check, valve PWM phase) | Hard | PA1 | PB3 (D3) | GPIO4 | GPIO18 |
| Control (PI → valve duty, every 10th sample) | Hard | PA4 | PB5 (D4) | GPIO5 | GPIO19 |
| Console (UART commands) | Firm | PA5 | PB4 (D5) | GPIO6 | GPIO20 |
| Telemetry (CSV line, 1 Hz) | Soft | PA6 | PB10 (D6) | GPIO7 | GPIO21 |
| Flow batch math (every 100 pulses) | — | PA7 | PA8 (D7) | GPIO15 | GPIO22 |
| Display HMI (optional, 2 Hz + on input) | Soft | PC14 | PA9 (D8) | GPIO17 | GPIO23 |

On the Nucleo and both ESP32 devkits the six pins are consecutive on one header,
so an 8-channel analyzer ribbon clips on in a single move. The Nucleo's run is
Arduino D3–D8, which is also ST Morpho CN10 pins 31 down to 21 — either header
works.

| | C0116-DK | Nucleo-L476RG | ESP32-S3 | ESP32-C6 |
|---|---|---|---|---|
| Valve (`led0`, software-PWM at 50 Hz) | user LED | LD2 (PA5) | GPIO21 | GPIO11 |
| Flow pulse input (internal pull-down) | PB7 | PC7 (D9) | GPIO16 | GPIO3 |
| Console | ST-LINK VCP | ST-LINK VCP | `uart0`, **UART** jack | `uart0`, **UART** jack |

115200 8N1. On the ESP32 devkits the console comes out of the `UART` jack, not
the `USB` jack beside it.

## Build and run

```bash
west build -p -b stm32c0116_dk                  firmware/superloop && west flash
west build -p -b nucleo_l476rg                  firmware/superloop && west flash
west build -p -b esp32s3_devkitc/esp32s3/procpu firmware/superloop && west flash
west build -p -b esp32c6_devkitc/esp32c6/hpcore firmware/superloop && west flash
west build -p -b native_sim                     firmware/superloop && ./build/zephyr/zephyr.exe
```

`native_sim` is for development only — no pins wired, and its timing is not real.

## The plant

Pressure comes from a synthetic plant (it drifts toward whatever the valve
commands), which is what weeks 2–4 measure against. To read a real pot instead,
wire the wiper to the pot channel (S3 GPIO2 · C6 GPIO1 · Nucleo A0), the ends to
3V3 and GND, and build with the fragment for your board:

```bash
west build ... -- -DEXTRA_DTC_OVERLAY_FILE=pot_esp32.overlay
west build ... -- -DEXTRA_DTC_OVERLAY_FILE=pot_nucleo.overlay
```

The C0116-DK reads its on-board joystick and the pot without the fragment.

## Wiring (C0116-DK)

- **Joystick** (on-board, PA8/ADC): left/right = HMI page · up/down = setpoint
  ±10 mV (dashboard page) · down **held ~1 s** = **e-stop** · center = clear.
  Calibrate the bands with the `joy` command.
- **"Pressure" pot** (breadboard kit) → PA0 (wiper), 3V3, GND.
- **Flow pulse input** → PB7 (the YF-S401 signal, a button module, or a square wave).

## Display (optional HMI)

SSD1306 128×64 OLED on I²C. Default pins: Nucleo SDA PB9 / SCL PB8 (D14/D15),
S3 SDA GPIO8 / SCL GPIO9, C6 SDA GPIO6 / SCL GPIO7. On the C0116-DK the font
tables overflow the 32 KB flash, so the HMI needs one of the larger boards.
Three pages — dashboard / flow / health. Build with the fragment:

```bash
west build ... -- -DEXTRA_CONF_FILE=display.conf
```

Without the fragment (or the module unplugged) the node runs identically minus
the HMI. Every refresh pushes the whole 1 KB frame over I²C from inside the
loop — watch `instr_disp` next to `instr_samp` on the analyzer and draw your
own conclusions.

## Console commands

`help` · `status` · `joy` (raw mV, for calibrating the bands) · `set <mv>`
(setpoint) · `clear` (e-stop) · `calib` — the **blocking command**: a zero-flow
calibration that takes several hundred ms. Week 2's Task C triggers it while
capturing; explaining *what* it degrades and *why* is the lab.

`status` also reports `backlog_peak`: the worst number of 1 kHz ticks that ever
piled up waiting for the loop to come around. It is the analyzer's jitter reading
in ticks, measured by the firmware itself.

## Week 3

The port is one file: `boards/<board>.overlay`. Pins and ADC channels move, the C
does not. Every piece here (ISR flags, polled work, batch bottom halves) has a
named counterpart in module 2's migration — that mapping is the week-3/4 talks.
