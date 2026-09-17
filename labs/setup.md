# Pre-course setup — before week 1

Work **at home, before the first class**. Week 1 only *verifies* this; if you show up without the environment, you lose the lab.

## 1. Zephyr environment

Follow Zephyr's official guide ([Getting Started](https://docs.zephyrproject.org/latest/develop/getting_started/index.html))
on native Linux or WSL2. If you're on WSL2, I keep a tested guide: [wsl2-embedded-dev-setup](https://github.com/saacifuentesmu/wsl2-embedded-dev-setup) — its [Zephyr page](https://github.com/saacifuentesmu/wsl2-embedded-dev-setup/blob/main/platforms/zephyr.md) takes you all the way to a `hello_world` running on an ESP32.

By the end you must have: `west`, the Zephyr SDK, and a working workspace (`west init` + `west update`).

## 2. Build, without any hardware

This one is mandatory and needs no board — `native_sim` builds a Linux executable:

```bash
cd ~/zephyrproject/zephyr
west build -p -b native_sim samples/hello_world
./build/zephyr/zephyr.exe
```

`native_sim` builds a **32-bit** binary. If this fails with `bits/libc-header-start.h: No such file or directory`, you are missing the 32-bit headers: `sudo apt install gcc-multilib g++-multilib`.

## 3. Build for a real board

Use whatever you already own. The course's two targets are the **NUCLEO-L476RG**
(weeks 1–2 — the board from the prerequisite courses) and the **ESP32-S3**
(week 3 on, [BOM](../BOM.md)),
but *any* ESP32 devkit is enough to practice the build→flash→monitor cycle now.

Install the toolchain for the architecture you're targeting first — the Zephyr SDK
does not install all of them by default:

```bash
west sdk install -t arm-zephyr-eabi                   # NUCLEO-L476RG
west sdk install -t xtensa-espressif_esp32s3_zephyr-elf   # ESP32-S3
```

```bash
west build -p -b nucleo_l476rg samples/basic/blinky        # course starter board
west build -p -b esp32s3_devkitc/esp32s3/procpu samples/hello_world
west flash
```

`Unable to find a valid toolchain` means you skipped the `west sdk install` above.

Other ESP32 targets: `esp32_devkitc/esp32/procpu`, `esp32c3_devkitm`,
`esp32c6_devkitc/esp32c6/hpcore`. `west boards | grep esp32` lists them all.

ESP32 on Zephyr needs two extra steps the generic Getting Started doesn't mention —
`west blobs fetch hal_espressif` and the matching SDK toolchain. Both are in the
[Zephyr page](https://github.com/saacifuentesmu/wsl2-embedded-dev-setup/blob/main/platforms/zephyr.md)
of the WSL2 guide.

## 4. Serial-port access (Linux/WSL2)

```bash
sudo usermod -aG dialout $USER   # log out and back in
```

On WSL2: install `usbipd-win` on Windows to pass USB through to Linux (covered in
the guide).

## 5. (Optional) CI/CD Integration with GitHub Actions

To practice the automated "evidence-first" culture used in industry, you can automate your `native_sim` builds. Create a `.github/workflows/build.yml` in your repository:

name: Zephyr Build
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    container: zephyrprojectrtos/ci:latest
    steps:
      - uses: actions/checkout@v3
      - name: Initialize Zephyr workspace
        run: |
          west init -l .
          west update
      - name: Build native_sim
        run: west build -p -b native_sim firmware/superloop

## Week-1 checklist

- [ ] `west --version` responds (after `source ~/zephyrproject/.venv/bin/activate`).
- [ ] `hello_world` runs on `native_sim`.
- [ ] Something builds for a real board — blinky on `nucleo_l476rg`, or
      `hello_world` on the ESP32 you have.
- [ ] Your user is in the `dialout` group (or equivalent).
- [ ] VS Code with the C/C++ extension (or your preferred editor) opens the workspace.

Problems? Bring the exact error (text, not a photo) to week 1 — the lab starts by
fixing whatever is missing.
