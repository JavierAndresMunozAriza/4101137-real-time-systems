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
| **05** | *Tracing & Per-Task Execution* | `[~]` Corrido en la placa (`threads` y trazas CTF) · capturas del analizador pendientes | [lab05_tracing.md](labs/lab05_tracing.md) |
| **06** | *Schedulability Theory & RTA* | `[~]` Firmware, scripts e informe listos · capturas pendientes | [lab06_schedulability.md](labs/lab06_schedulability.md) |
| **07** | *Priority Inversion & Priority Inheritance* | `[~]` Firmware, script RTA e informe listos · capturas pendientes | [lab07_rta_inversion.md](labs/lab07_rta_inversion.md) |
| **08-13** | *Advanced RTOS, Preempt RT & Project Kickoff* | `[ ]` Pendiente | [labs/](labs/) |

## 📌 Destacados Técnicos (Laboratorios Completados)

### 📌 Laboratorio 02: Arquitectura Superloop y Mediciones de Tiempo Real
- **Diseño del Firmware:** Implementación de temporización basada en ISR con divisor de frecuencia en C (`firmware/superloop/src/main.c`).
- **Análisis de Desempeño:**
  - Jitter de muestreo nominal: 1.8 µs (con acelerador de caché ART activo).
  - Impacto del Caché Flash: Sin acelerador de caché (`nocache.conf`), el jitter aumentó a 5.6 µs (+3.8 µs de degradación).
  - Análisis de Sobrecarga: Un comando bloqueante síncrono expandió el periodo de muestreo a 1200 µs, acumulando 6 ticks de backlog en el sistema.
- **Requerimientos EARS:** Especificación y validación directa entre la instrumentación GPIO con analizador lógico y la rúbrica EARS (`REQ-SAMP-01`, `REQ-LAT-01`).

### 📌 Laboratorio 03: Port a ESP32-S3 y Primer Thread (En Progreso)
- **Task A (Portabilidad Devicetree):** Migración del firmware `superloop` al microcontrolador ESP32-S3 mediante Devicetree Overlay (`boards/esp32s3_devkitc_esp32s3_procpu.overlay`). Portabilidad lograda con **0 cambios en el código C (`main.c`)**.

## 🚧 Laboratorios 05–07 (en progreso)

> Los tres tienen firmware compilado, scripts reproducibles e informe en el formato del Lab 04. El Lab 05 ya corrió en la placa (`threads` y trazas CTF); las capturas de **PulseView** del Lab 05 y las **mediciones** de los Labs 06–07 aún no se toman: en los informes están marcadas `⏳` y las cifras *predichas* están rotuladas como tales (nada se estima como si fuera medido).

### 📌 Laboratorio 05: Tracing (CTF sobre USB-Serial-JTAG)
- **Ruta adaptada:** el SOP asume un segundo adaptador USB-UART; el nodo solo tiene la ESP32-S3, así que el stream CTF sale por el periférico **USB-Serial-JTAG** (jack USB) y la consola queda en el jack UART. Sin hardware adicional.
- **Firmware (`firmware/kernel`):** hilos con nombre legible, estado compartido en `atomic_t`, comando `threads` con stack y **CPU % por hilo** (`CONFIG_THREAD_RUNTIME_STATS`), y `notrace.conf` para el A/B del costo del tracing (Task C).
- **Primera ejecución en hardware:** el firmware se caía a los ~2 s (stack de `thread_analyzer` desbordado con tracing); corregido y verificado. `babeltrace2 2.1.2` aborta con este `metadata`, así que la traza se decodifica con `tools/scripts/ctf_decode.py`.
- **Hallazgos medidos:** la traza es una muestra con pérdidas (≈ 1.8 % de los ticks, ≈ 4.8 KB/s), su reloj se envuelve cada 76.86 s y `tracing_thread` consume el 39 % de la CPU. La preemption de `console` durante `calib` no quedó registrada.
- **Pendiente (analizador lógico):** latencia por GPIO (D0/D1) y el A/B del jitter con tracing ON/OFF.

### 📌 Laboratorio 06: Schedulability
- **Scripts (`tools/scripts/`):** `sched_sim.py` (tests de utilización Liu & Layland / hiperbólico y simulador RM vs EDF) y `loop_jitter_sim.py` (predicción del error de lazo vs jitter).
- **Resultado calculado (simulación):** el conjunto sintético de Task A (U = 0.967) hace que **RM pierda `tau3` a los 150 ms** y que **EDF cumpla** (peor respuesta 140 ms). Ambos tests suficientes para RM fallan, así que no deciden: hace falta la simulación o el RTA exacto.
- **Firmware:** `firmware/sched_labs` (RM y EDF con `CONFIG_SCHED_DEADLINE`, cómputo = espera activa calibrada) y, en el nodo, `CONFIG_LAB06_JITTER_MAX_MS` para inyectar jitter de actuación.
- **Supuesto declarado:** los parámetros de la Fig. 4.13 y las tareas de Daniela no están en el repo; el conjunto de Task A es sintético y verificado por simulación.

### 📌 Laboratorio 07: RTA e Inversión de Prioridad
- **`tools/scripts/rta.py`:** recurrencia de Buttazzo (Fig. 4.17) con bloqueo $B_i$. Para el nodo (C_i del Lab 02), `control` tiene $R$ = 22.7 µs frente a su deadline de 5 ms.
- **Escenario (predicción):** `control` con deadline de 5 ms → sin lock 1 ms · `k_mutex` con herencia ≤ 5 ms · `k_sem` como lock (inversión) ≈ 14–15 ms, fuera de deadline.
- **ADR-002 (Proposed):** locks entre prioridades con `k_mutex`; `k_sem` solo para señalizar; presupuesto de sección crítica de 4 ms para el camino del e-stop.

### 🔧 Cómo reproducir los cálculos

```bash
python3 tools/scripts/sched_sim.py sim --timeline 30      # Lab 06, Task A: RM vs EDF
python3 tools/scripts/sched_sim.py tests --preset node    # Lab 06, Task B: U y tests
python3 tools/scripts/loop_jitter_sim.py --sweep          # Lab 06, Task C: error vs jitter
python3 tools/scripts/rta.py --preset lab07-pi --trace    # Lab 07: RTA con bloqueo
python3 tools/scripts/ctf_decode.py $ZEPHYR_BASE/subsys/tracing/ctf/tsdl/metadata channel0_0 > trace.txt   # Lab 05: traza CTF
python3 tools/scripts/ctf_preempt_stats.py trace.txt      # Lab 05: despertares de sampling (t0..t3)
```

Los firmwares de los labs 06–07 se seleccionan por Kconfig (`-DCONFIG_SCHED_LAB_RM=y`, `..._EDF`, `..._NOLOCK`, `..._INVERSION`, `..._PI`); los comandos exactos están en cada informe.
