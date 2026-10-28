# Lab 04 / Week 4 — Migration to Preemptive Kernel & A/B Benchmark

## §1 Tabla Comparativa de Mediciones (A/B Test)

| Medición (ESP32-S3) | Superloop (Semana 3) | Kernel Thread (Semana 4) |
| :--- | :---: | :---: |
| **Max sampling jitter (nominal)** | 0.16 µs | 0.08 µs |
| **Max sampling jitter (con `calib` bloqueante)** | > 100 µs (degradado) | 0.08 µs (inmune) |
| **ISR → thread latency (vía `k_msgq`)** | 1.25 µs | 0.05 µs (45 ns) |
| **Overhead: Ancho visible de cambio de contexto** | N/A | 0.05 µs (45 ns) |
| **Overhead estimado por segundo (a 1 kHz)** | 0 % | 0.01 % |
| **Control/Sampling Thread Stack High-Water Mark** | N/A | 416 B / 1536 B (27% usado) |

---

## §2 ADR-001 — Node Architecture: Multithreaded Kernel

* **Status:** Accepted
* **Date:** 2026-10-27
* **Deciders:** Real-Time Systems Engineering Team

### Context
La arquitectura superloop original presentaba vulnerabilidad crítica frente a tareas de cómputo intensivo o comandos de consola bloqueantes como `calib`. Bajo estas condiciones de carga en la CPU, el jitter del ciclo de muestreo se degradaba drásticamente superando los $100\,\mu\text{s}$, destruyendo las garantías de tiempo real necesarias para el control preciso de SoilSense.

### Decision
Migrar el nodo ESP32-S3 de una arquitectura por sondeo (superloop) a un sistema multihilo preemptivo guiado por el **Zephyr Kernel**:
1. **`sampling_thread` (Prioridad 2, Stack 1536B):** Hilo de alta prioridad dedicado a la adquisición e integración periódica ($1000\text{ Hz}$), sincronizado mediante `k_msgq`.
2. **`k_work` en System Workqueue:** Procesamiento diferido de la interrupción del medidor de flujo (`instr_flow`).
3. **Console Thread (Prioridad 10):** Consola interactiva relegada a prioridad baja en `main` para evitar la interrupción del lazo de control.

### Justification
La evaluación experimental bajo el protocolo A/B confirma que:
* **Determinismo:** El jitter máximo de muestreo en el Kernel Thread permanece inmune a la ejecución de `calib`, manteniéndose constante en **$0.08\,\mu\text{s}$**.
* **Latencia de Respuesta:** La latencia de atención a eventos decreció de **$1.25\,\mu\text{s}$** a **$0.05\,\mu\text{s}$** ($45\text{ ns}$), gracias a la conmutación preemptiva inmediata del planificador.

### Consequences & Costs
* **Beneficios:** Aislamiento total de las tareas críticas en tiempo real frente a perturbaciones en la consola o bucles intensivos de usuario.
* **Costos:**
  * **Carga de CPU por Cambio de Contexto:** $0.05\,\mu\text{s}$ por conmutación. A $1000\text{ Hz}$ representa un overhead insignificante del **$0.01\%$** del tiempo total de la CPU.
  * **Consumo de Memoria Stack:** Asignación explícita de pilas por hilo, auditada mediante `CONFIG_THREAD_ANALYZER`.

---

## §3 Week-4 Evidence

### Task A — Migración Completa de la Arquitectura
* **Estructura del Nodo:** El hilo `main` actúa únicamente como inicializador y despacho de consola de baja prioridad.
* **Instrumentación Simétrica:** Se preservaron las salidas de prueba **D0 (`instr_samp` - GPIO 4)** y **D1 (`instr_flow` - GPIO 15)** para garantizar simetría metodológica frente al superloop.

### Task B — Comparativa A/B y Capturas
* **Canal D0 (`instr_samp`):** Tren de pulsos estrictamente periódico de $1.000\text{ ms}$ ($1000\,\mu\text{s}$). El jitter se mantiene en $0.08\,\mu\text{s}$ incluso con la función `calib` saturando la CPU.
* **Canal D1 (`instr_flow`):** Pulso de respuesta que conmuta $45\text{ ns}$ ($0.05\,\mu\text{s}$) posterior al evento disparador.

### Task C — Medición de Costo y Cálculo de Overhead

El costo computacional introducido por la conmutación de hilos se determina mediante:

$$\text{Conmutaciones por segundo} = 1000 \text{ Muestras/s} \times 2 \text{ (Entrada y Salida)} = 2000 \text{ switches/s}$$

$$\text{Tiempo de overhead acumulado} = 2000 \text{ switches/s} \times 0.05\,\mu\text{s} = 100\,\mu\text{s/s}$$

$$\text{Overhead total de CPU} = \frac{100\,\mu\text{s}}{1\,000\,000\,\mu\text{s}} \times 100\% = 0.01\%$$

### Task D — Seguridad de Memoria (Stack High-Water Mark)

Salida de diagnóstico obtenida mediante `CONFIG_THREAD_ANALYZER=y` y `CONFIG_INIT_STACKS=y` en ejecución bajo carga:

```text
Thread analyze:
 : unused / stack size [% used]
 sampling_thread: 1120 / 1536 [27%]
 main           :  768 / 1024 [25%]
 sys_workq      :  352 /  512 [31%]
 idle 00        :  220 /  320 [31%]
```
 **Conclusión de Seguridad:** Ningún hilo excede el 31% del stack asignado, confirmando un margen de seguridad superior al 68% contra riesgos de stack overflow.