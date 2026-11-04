# Real-Time Embedded Systems Portfolio — Zephyr RTOS & STM32

Este repositorio contiene las prácticas, arquitecturas de software e investigaciones sobre Sistemas en Tiempo Real (RTOS) desarrolladas durante el curso. Incluye la implementación de firmware en C sobre la plataforma Zephyr RTOS, análisis de tiempos en hardware real (STM32 NUCLEO-L476RG y ESP32-S3), instrumentación física con analizador lógico y análisis de requerimientos bajo el estándar EARS.

## 👤 Autor

- **Estudiante:** Javier Andrés Muñoz Ariza
- **Plataforma Objetivo:** STM32L476RG (ARM Cortex-M4) / ESP32-S3
- **RTOS & Toolchain:** Zephyr RTOS | West | OpenOCD | WSL2 (Ubuntu)
- **Instrumentación:** Analizador Lógico USB (8 Canales) + PulseView

## 🛠️ Tecnologías y Herramientas

- **Sistemas Operativos en Tiempo Real:** Zephyr RTOS (Superloop, Threads, Preemption, IPC)
- **Lenguaje:** C (Embedded Systems)
- **Hardware:** STM32 NUCLEO-L476RG / ESP32-S3 DevKit
- **Herramientas de Análisis:** PulseView / Logic Analyzer (Medición de jitter, latencia ISR, C_i y T_i)
- **Control de Versiones & Entorno:** Git, GitHub, Linux / WSL2, VS Code

## 🚦 Estado de Avance de Laboratorios

| Lab | Tema / Arquitectura | Estado | Reporte |
| :---: | :--- | :---: | :---: |
| **01** | *Bring-up: Environment, Toolchain & Board* | `[x]` Completado | [lab01_bringup.md](labs/lab01_bringup.md) |
| **02** | *Superloop Architecture, Cache Jitter & Measurement* | `[x]` Completado | [lab02_superloop.md](labs/lab02_superloop.md) |
| **03** | *The S3 Port and the First Thread* | `[x]` Completado | [lab03_port.md](labs/lab03_port.md) |
| **04** | *Full Migration and A/B Testing* | `[x]` Completado | [lab04_ipc.md](labs/lab04_ipc.md) |
| **05** | *Tracing & Per-Task Execution* | `[~]` Medido en la placa (`threads`, trazas CTF, captura GPIO de `calib`) · falta el A/B del jitter con tracing ON/OFF | [lab05_tracing.md](labs/lab05_tracing.md) |
| **06** | *Schedulability: Fig. 4.13, Inversión de Prioridad y ADR-005* | `[x]` Completo, con pendientes declarados: parámetros de Daniela y repetir la Task A oficial en nuestra placa | [lab06_schedulability.md](labs/lab06_schedulability.md) |
| **07** | *RTA & Priority Inversion* | `[~]` En curso: la inversión ya se midió como Task B del Lab 06 y los C_i del S3 están medidos; el informe sigue como borrador (guía oficial no consultada) | [lab07_rta_inversion.md](labs/lab07_rta_inversion.md) |
| **08-13** | *Advanced RTOS, Preempt RT & Project Kickoff* | `[ ]` Pendiente | [labs/](labs/) |

## 📌 Destacados Técnicos (Laboratorios Completados)

### 📌 Laboratorio 02: Arquitectura Superloop y Mediciones de Tiempo Real
- **Diseño del Firmware:** Implementación de temporización basada en ISR con divisor de frecuencia en C (`firmware/superloop/src/main.c`).
- **Análisis de Desempeño:**
  - Jitter de muestreo nominal: 1.8 µs (con acelerador de caché ART activo).
  - Impacto del Caché Flash: Sin acelerador de caché (`nocache.conf`), el jitter aumentó a 5.6 µs (+3.8 µs de degradación).
  - Análisis de Sobrecarga: Un comando bloqueante síncrono expandió el periodo de muestreo a 1200 µs, acumulando 6 ticks de backlog en el sistema.
- **Requerimientos EARS:** Especificación y validación directa entre la instrumentación GPIO con analizador lógico y la rúbrica EARS (`REQ-SAMP-01`, `REQ-LAT-01`).

### 📌 Laboratorio 03: Port a ESP32-S3 y Primer Thread
- **Task A (Portabilidad Devicetree):** Migración del firmware `superloop` al microcontrolador ESP32-S3 mediante Devicetree Overlay (`boards/esp32s3_devkitc_esp32s3_procpu.overlay`). Portabilidad lograda con **0 cambios en el código C (`main.c`)**.

### 📌 Laboratorio 04: Migración al kernel preemptivo y prueba A/B
- **Arquitectura (ADR-001, Accepted):** el nodo pasa del superloop a hilos de Zephyr: `sampling` (prioridad 2, 1 kHz, sincronizado con `k_msgq`), el lote de flujo diferido con `k_work` y la consola en prioridad baja.
- **Resultado A/B en el ESP32-S3:** jitter máximo de muestreo 0.16 µs (superloop) frente a **0.08 µs** (kernel); con `calib` bloqueante el superloop se degrada a más de 100 µs y el kernel sigue en **0.08 µs**.
- **Latencia ISR → hilo:** de 1.25 µs a **0.05 µs (45 ns)**; costo del cambio de contexto ≈ 0.01 % de la CPU a 1 kHz.

## 📌 Laboratorios 05–07

> Labs 05 y 06 medidos en la placa; el 06 está completo con pendientes declarados y el 07 sigue en curso (estado de cada uno en la tabla de arriba). Lo no medido está marcado `⏳` en los informes y las cifras *predichas* están rotuladas como tales: nada se estima como si fuera medido.

### 📌 Laboratorio 05: Tracing (CTF sobre USB-Serial-JTAG)
- **Ruta adaptada:** el SOP asume un segundo adaptador USB-UART; el nodo solo tiene la ESP32-S3, así que el stream CTF sale por el periférico **USB-Serial-JTAG** (jack USB) y la consola queda en el jack UART. Sin hardware adicional.
- **Firmware (`firmware/kernel`):** hilos con nombre legible, estado compartido en `atomic_t`, comando `threads` con stack y **CPU % por hilo** (`CONFIG_THREAD_RUNTIME_STATS`), y `notrace.conf` para el A/B del costo del tracing (Task C).
- **Primera ejecución en hardware:** el firmware se caía a los ~2 s (stack de `thread_analyzer` desbordado con tracing); corregido y verificado. `babeltrace2 2.1.2` aborta con este `metadata`, así que la traza se decodifica con `tools/scripts/ctf_decode.py`.
- **Hallazgos medidos:** la traza es una muestra con pérdidas (≈ 1.8 % de los ticks, ≈ 4.8 KB/s), su reloj se envuelve cada 76.86 s y `tracing_thread` consume el 39 % de la CPU. La preemption de `console` durante `calib` no quedó registrada.
- **Captura por GPIO:** durante `calib`, D2 (`console`) queda 403.5 ms en alto mientras D0 (`sampling`) sigue pulsando.
- **Pendiente (analizador lógico):** el A/B

### 📌 Laboratorio 06: Schedulability, inversión de prioridad y ADR-005 (completo)
> La guía oficial actual (repositorio del curso) difiere de la copia que había en este repositorio: pide la Fig. 4.13 con τ1 (C = 2 ms, T = 5 ms) y τ2 (C = 4 ms, T = 7 ms), la inversión de prioridad y el ADR-005. El informe se alineó con ella.

- **Task A (Fig. 4.13):** simulación (`sched_sim.py sim --tasks "2:5,4:7"`): RM pierde `tau2` en t = 7 ms y EDF cumple. Las capturas del conjunto oficial son del compañero de pareja (WaveForms) y van atribuidas. En nuestra placa se midió además un conjunto sintético de tres tareas: D3 sube 149.997 ms después del primer pulso de `tau1` bajo RM; EDF cumple.
- **Task B (inversión), medida en la placa:** `control` responde en **1.013 ms** sin lock, **14.037 ms** con `k_sem` (301 de 301 fallos) y **4.038 ms** con `k_mutex`; **B = 4.024 ms**. Tres capturas anotadas. La RTA da cotas de 1, 15 y 5 ms; lo medido queda por debajo porque el episodio repite siempre el mismo desfase.
- **Task C (ADR-005):** el e-stop pasó de **hasta 10.005 ms** (6 disparos) a **4.0 µs** (2 disparos) al poner `duty_pct = 0` en el mismo trabajo de `sampling`; `calib` pasó de 387 ms de CPU a **13 ms** con `k_usleep` y tick de 10 kHz (con 1 kHz tardaba 2 000 ms y no cabía en 1 s).
- **Nodo medido en el ESP32-S3:** C = 11.7 µs (`sampling`), 4.0 µs (`control`), 65 µs (telemetría); U = 0.0122, planificable con RM.
- **ADR-002 y ADR-005** redactados en el informe (§2.1). Sesiones de PulseView en `labs/captures/` y scripts en `tools/scripts/` (`estop_latency.py`, `sr_pulse_widths.py`).
- **Stretch (no calificado):** el error del lazo con jitter inyectado de 0, 2 y 5 ms es 12 mV en los tres casos.
- **Pendientes declarados:** los parámetros de las tareas de Daniela (datos de clase, no disponibles), 3 disparos más «después» para llegar a 5, y adaptar `sched_labs` al conjunto τ1/τ2 para repetir la Task A oficial en nuestra placa.

### 📌 Laboratorio 07: RTA e Inversión de Prioridad (en curso)
- **Estado:** la inversión de prioridad ya se midió en la placa como Task B del Lab 06 (guía oficial): R máx de `control` = 1.013 / 14.037 / 4.038 ms (sin lock / `k_sem` / `k_mutex`), B = 4.024 ms. Los C_i del ESP32-S3 también están medidos (Lab 06 §4.1). El informe del Lab 07 sigue como borrador: la guía oficial del Lab 07 no se pudo consultar.
- **`tools/scripts/rta.py`:** recurrencia de Buttazzo (Fig. 4.17) con bloqueo $B_i$; sus cotas (1, 15 y 5 ms) están comparadas con lo medido en el Lab 06 §4.4.
- **ADR-002 (Accepted, en el informe del Lab 06):** locks entre prioridades con `k_mutex`; `k_sem` solo para señalizar; B medido = 4.024 ms.

### 🔧 Cómo reproducir los cálculos

```bash
python3 tools/scripts/sched_sim.py sim --timeline 30      # Lab 06, Task A: RM vs EDF
python3 tools/scripts/sched_sim.py tests --preset node    # Lab 06, Task B: U y tests
python3 tools/scripts/loop_jitter_sim.py --sweep          # Lab 06, Task C: error vs jitter
python3 tools/scripts/rta.py --preset lab07-pi --trace    # Lab 07: RTA con bloqueo
python3 tools/scripts/ctf_decode.py $ZEPHYR_BASE/subsys/tracing/ctf/tsdl/metadata channel0_0 > trace.txt   # Lab 05: traza CTF
python3 tools/scripts/ctf_preempt_stats.py trace.txt      # Lab 05: despertares de sampling (t0..t3)
python3 tools/scripts/estop_latency.py labs/captures/lab06_taskC_estop_before.sr   # Lab 06 Task C: e-stop detección → válvula
python3 tools/scripts/sr_pulse_widths.py labs/captures/lab06_taskC_estop_before.sr  # C_i del nodo en el S3
```

Los firmwares de los labs 06–07 se seleccionan por Kconfig (`-DCONFIG_SCHED_LAB_RM=y`, `..._EDF`, `..._NOLOCK`, `..._INVERSION`, `..._PI`); los comandos exactos están en cada informe.
