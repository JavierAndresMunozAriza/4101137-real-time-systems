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
| **06** | *Schedulability: Fig. 4.13, Inversión de Prioridad y ADR-005* | `[~]` Task B medida · Task C: arreglos hechos y «antes» medido, falta la captura «después» · faltan los parámetros de Daniela | [lab06_schedulability.md](labs/lab06_schedulability.md) |
| **07** | *RTA & Priority Inversion (guía oficial no consultada)* | `[~]` La inversión de prioridad se midió como Task B del Lab 06; el informe del Lab 07 sigue como borrador | [lab07_rta_inversion.md](labs/lab07_rta_inversion.md) |
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
- **Captura por GPIO:** durante `calib`, D2 (`console`) queda 403.5 ms en alto mientras D0 (`sampling`) sigue pulsando.
- **Pendiente (analizador lógico):** el A/B del jitter de D0 con tracing ON/OFF. La guía oficial vigente pide sacar el CTF por el jack UART a 4 Mbaud (aquí se usó el USB-Serial-JTAG), que probablemente recupera los eventos que esta ruta pierde.

### 📌 Laboratorio 06: Schedulability, inversión de prioridad y ADR-005
> La guía oficial actual (repositorio del curso) difiere de la copia que había en este repositorio: pide la Fig. 4.13 con τ1 (C = 2 ms, T = 5 ms) y τ2 (C = 4 ms, T = 7 ms), la inversión de prioridad y el ADR-005. El informe se alineó con ella.

- **Task A (Fig. 4.13):** simulación (`sched_sim.py sim --tasks "2:5,4:7"`): RM pierde `tau2` en t = 7 ms y EDF cumple. Las capturas del conjunto oficial son del compañero de pareja (WaveForms) y van atribuidas. En nuestra placa se midió además un conjunto sintético de tres tareas: D3 sube 149.997 ms después del primer pulso de `tau1` bajo RM; EDF cumple (peor respuesta de `tau3` = 140.103 ms).
- **Task B (inversión), medida en la placa:** `control` responde en **1.013 ms** sin lock, **14.037 ms** con `k_sem` (301 de 301 fallos de deadline) y **4.038 ms** con `k_mutex`; **B = 4.024 ms**. Tres capturas anotadas en `labs/img/`.
- **Task C (ADR-005):** *Antes:* detección → válvula baja = **7.005, 10.005, 2.004, 2.999, 1.004 y 2.004 ms** (máx **10.005 ms**, no cabe en 5 ms); `calib` = 405 ms de reloj y 387 ms de CPU. *Arreglos en `firmware/kernel`:* `k_usleep` en `calib` con tick de 10 kHz (500 ms de reloj, 13 ms de CPU; con 1 kHz eran 2 000 ms, fuera del plazo de 1 s) y `duty_pct = 0` en la rama del e-stop. Falta la captura del e-stop «después».
- **Stretch (no calificado):** el error del lazo con jitter inyectado de 0, 2 y 5 ms es 12 mV en los tres casos.
- **Pendiente:** parámetros de las tareas de Daniela (§4.2), ADR-002 y ADR-005 redactados, y adaptar `sched_labs` al conjunto τ1/τ2.

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
