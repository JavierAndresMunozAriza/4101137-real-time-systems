# Real-Time Embedded Systems Portfolio — Zephyr RTOS & STM32

Este repositorio contiene las prácticas, arquitecturas de software e investigaciones sobre **Sistemas en Tiempo Real (RTOS)** desarrolladas durante el curso. Incluye la implementación de firmware en C sobre la plataforma **Zephyr RTOS**, análisis de tiempos en hardware real **STM32 (NUCLEO-L476RG)**, instrumentación física con analizador lógico y análisis de requerimientos bajo el estándar **EARS**.

---

## 👤 Autor

* **Estudiante:** Javier Andrés Muñoz Ariza
* **Plataforma Objetivo:** STM32L476RG (ARM Cortex-M4)
* **RTOS & Toolchain:** Zephyr RTOS | West | OpenOCD | WSL2 (Ubuntu)
* **Instrumentación:** Analizador Lógico USB (8 Canales) + PulseView

---

## 🛠️ Tecnologías y Herramientas

* **Sistemas Operativos en Tiempo Real:** Zephyr RTOS (Superloop, Threads, Preemption, IPC)
* **Lenguaje:** C (Embedded Systems)
* **Hardware:** STM32 NUCLEO-L476RG
* **Herramientas de Análisis:** PulseView / Logic Analyzer (Medición de jitter, latencia ISR, $C_i$ y $T_i$)
* **Control de Versiones & Entorno:** Git, GitHub, Linux / WSL2, VS Code

---

## 📚 Estado de Avance de Laboratorios

| Lab | Tema / Arquitectura | Estado | Reporte |
| :---: | :--- | :---: | :---: |
| **01** | *Bring-up: Environment, Toolchain & Board* | `[x] Completado` | [lab01_bringup.md](labs/lab01_bringup.md) |
| **02** | *Superloop Architecture, Cache Jitter & Measurement* | `[x] Completado` | [lab02_superloop.md](labs/lab02_superloop.md) |
| **03** | *The S3 Port and the First Thread* | `[ ] Pendiente` | [lab03_port.md](labs/lab03_port.md) |
| **04** | *Full Migration and A/B Testing* | `[ ] Pendiente` | [lab04_ipc.md](labs/lab04_ipc.md) |
| **05** | *Tracing & Per-Task Execution* | `[ ] Pendiente` | [labs/lab05_tracing.md](labs/lab05_tracing.md) |
| **06** | *Schedulability Theory & RTA* | `[ ] Pendiente` | [labs/lab06_schedulability.md](labs/lab06_schedulability.md) |
| **07** | *Priority Inversion & Priority Inheritance* | `[ ] Pendiente` | [labs/lab07_rta_inversion.md](labs/lab07_rta_inversion.md) |
| **08-13** | *Advanced RTOS, Preempt RT & Project Kickoff* | `[ ] Pendiente` | [labs/](labs/) |

---

## 🔬 Destacados Técnicos (Laboratorios Completados)

### 📌 Laboratorio 02: Arquitectura Superloop y Mediciones de Tiempo Real
* **Diseño del Firmware:** Implementación de temporización basada en ISR con divisor de frecuencia en C (`firmware/superloop/src/main.c`).
* **Análisis de Desempeño:**
  * **Jitter de muestreo nominal:** $1.8\ \mu\text{s}$ (con acelerador de caché ART activo).
  * **Impacto del Caché Flash:** Sin acelerador de caché (`nocache.conf`), el jitter aumentó a $5.6\ \mu\text{s}$ (+3.8 µs de degradación).
  * **Análisis de Sobrecarga:** Un comando bloqueante síncrono expandió el período de muestreo a $1200\ \mu\text{s}$, acumulando **6 ticks** de backlog en el sistema.
* **Requerimientos EARS:** Especificación y validación directa entre la instrumentación GPIO con analizador lógico y la rúbrica EARS (`REQ-SAMP-01`, `REQ-LAT-01`).