# Lab 07 / Week 7 — RTA e inversión de prioridad

> **Contexto del Proyecto:**
> *"Eng. Samuel Cifuentes: el test de utilización dice si el conjunto cabe; no dice **cuánto tarda cada tarea en el peor caso**, y el e-stop tiene un deadline de 5 ms, no un porcentaje. Implementa RTA como script, alimentado con los $C_i$ medidos. Y cuidado con los mutex: Pathfinder se reinició en Marte por una inversión de prioridad. Reprodúcela aquí, donde cuesta una risa y no una misión."*
>
> Referencia: Buttazzo, *Hard Real-Time Computing Systems* (4.ª ed.), §4.5 (RTA, Fig. 4.17) y cap. 7 · Firmware: `firmware/sched_labs/` · Script: `tools/scripts/rta.py` · Placa: ESP32-S3 DevKitC.

> **Estado de la evidencia:** el script RTA corre y sus salidas están abajo; el firmware compila en sus tres variantes. Las capturas del analizador y las lecturas de la placa **aún no se toman**: todo valor medido está marcado `⏳`. Los valores *predichos* salen del script y están rotulados como tales; ninguna medición se estima.

---

## §1 Tabla de Mediciones ("What You'll Measure")

Conjunto del escenario (ms): `control` (prioridad alta, $C$ = 1, deadline 5), `medium` ($C$ = 10), `slow` ($C$ = 4, todo dentro de su sección crítica). `slow` toma el lock en t = 0, `control` lo pide en t = 1 y `medium` se libera en t = 2. Tolerancia de lectura: ±1 tick (1 ms), por la resolución de los timers.

| Medición | Valor medido (placa) | Predicción |
| :--- | :---: | :---: |
| **Max R de `control` (sin mutex)** | ⏳ ____ µs | RTA: $R = C$ = **1 000 µs** |
| **Max R de `control` durante la inversión** | ⏳ ____ µs | modelo: $C_c + (C_s - \delta_c) + C_m$ = **14 000 µs**; cota RTA 15 000 µs |
| **Max R de `control` con mutex PI** | ⏳ ____ µs | RTA: $\le C + B_i + I$ = 1 + 4 + 0 = **5 000 µs**; esperado 4 000 µs |
| **$B_i$ real** (sección crítica más larga de la tercera tarea) | ⏳ ____ µs | $C_s$ = **4 000 µs** |

*(El máximo esperado de 4 000 µs con PI sale de $C_c + (C_s - \delta_c)$ = 1 + 3: la sección crítica de `slow` ya llevaba 1 ms cuando `control` la pidió. La cota de RTA usa el peor caso $B_i = C_s$.)*

---

## §2 ADR-002 — Node Mutual-Exclusion Policy

* **Status:** Proposed — se acepta cuando las capturas de §3 confirmen las predicciones.
* **Date:** 2026-11-03
* **Deciders:** Real-Time Systems Engineering Team

### Context
El e-stop tiene un deadline de **5 ms**. Si la tarea de control comparte un lock con una tarea de menor prioridad y no hay herencia de prioridad, una tarea de prioridad intermedia que no usa el lock puede desalojar al dueño y retrasar a control durante el tiempo que ella quiera: la inversión de prioridad que reinició a Pathfinder. En el escenario del Lab 07, la predicción para un `k_sem` de 1 usado como lock es $R_{control}$ = 14–15 ms, casi el triple del deadline.

### Decision
1. **Todo recurso compartido entre hilos de distinta prioridad se protege con `k_mutex`.** Zephyr implementa herencia de prioridad en `k_mutex` (`kernel/mutex.c`): el dueño hereda la prioridad del mejor de sus esperadores mientras tenga el lock.
2. **`k_sem` solo señaliza** (liberación de tareas, ISR → hilo). Nunca se usa como lock.
3. **Estado compartido con ISRs: `atomic_t`** (así está hoy el nodo en `firmware/kernel/src/main.c`). Un `k_mutex` no puede tomarse desde una ISR.
4. **Presupuesto de sección crítica.** De $R_i = C_i + B_i + I_i \le D_i$ con $D$ = 5 ms, $C_c$ = 1 ms e $I$ = 0: $B_{max} = D - C_c - I$ = **4 ms**. Toda sección crítica bajo un lock que `control` (o `sampling`) pueda tomar debe durar **≤ 4 ms**. El escenario del lab usa exactamente 4 ms, el límite: $R$ = 5 ms = $D$.
5. **No anidar locks** y no tomar desde `sampling`/`control` un lock que otros mantengan por más de $B_{max}$. Un hipotético lock de la UART, cuyo dueño imprime ≈ 7 ms a 115200 con polling (comentario de `telemetry` en `main.c`), queda fuera del alcance de las tareas duras.

### Justification
Predicción de RTA (`tools/scripts/rta.py`, mismo conjunto de tareas):

| Escenario | $R_{control}$ (ms) | $D$ (ms) | ¿Cumple? |
| :--- | :---: | :---: | :---: |
| Sin lock | 1 | 5 | ✔ |
| `k_mutex` (PI), $B_i$ = 4 | 5 | 5 | ✔ (en el límite) |
| `k_sem` (inversión): $B_i$ = 4 + `medium` 10 | 15 | 5 | ✘ |

**Medido:** ⏳ completar con §3 (las tres filas deben reproducir esta tabla dentro de ±1 ms).

### Consequences & Costs
* **Beneficio:** el bloqueo de `control` queda acotado a **una** sección crítica de una tarea de menor prioridad, sin depender de lo que hagan las tareas intermedias.
* **Costo 1 — límite de diseño:** con la herencia, $B_i$ no desaparece, solo se acota: el presupuesto de 4 ms es una restricción real sobre el código de las tareas lentas.
* **Costo 2 — alcance de la herencia:** `k_mutex` hereda prioridad, pero **no** implementa el protocolo de techo de prioridad completo del libro (`CONFIG_PRIORITY_CEILING` solo limita cuánto puede subir una herencia). No previene deadlock ni bloqueo encadenado: por eso la regla 5 prohíbe anidar locks.
* **Costo 3 — operación más cara que un semáforo:** el sobrecosto por llamada de `k_mutex_lock`/`unlock` frente a `k_sem_take`/`give` **no se midió**; a 100 ms de periodo es irrelevante, pero debe medirse antes de usar un mutex en el lazo de 1 kHz de `sampling`.

> **FreeRTOS.** Sus mutex (`xSemaphoreCreateMutex`) también heredan prioridad, y sus semáforos binarios permiten exactamente el error del Task B. La lección es idéntica en ambos kernels: para exclusión mutua entre prioridades, un mutex con herencia, nunca un semáforo.

---

## §3 Week-7 Evidence

### Task A — RTA como herramienta

`tools/scripts/rta.py` implementa la recurrencia de la Fig. 4.17 de Buttazzo, extendida con bloqueo:

$$R_i = C_i + B_i + \sum_{h \in hp(i)} \left\lceil \frac{R_i}{T_h} \right\rceil C_h$$

La iteración parte de $R_i = C_i + B_i$ y se detiene en el punto fijo (ese es el peor caso) o cuando $R_i > D_i$ (no planificable). Solo usa la biblioteca estándar de Python 3.

**Salida con los $C_i$ medidos del nodo** (Lab 02, L476RG, analizador lógico + PulseView; periodo de la ISR **supuesto**, el deadline de 5 ms de `control` es el del curso):

```text
$ python3 tools/scripts/rta.py --preset node --trace
unit: us   (tasks in decreasing priority order)
  flow_isr: iterations R = 4.2 -> 4.2
  sampling: iterations R = 6 -> 10.2 -> 10.2
  control: iterations R = 12.5 -> 22.7 -> 22.7
  telemetry: iterations R = 45 -> 67.7 -> 67.7
task                C         B           R           D  schedulable
flow_isr          4.2         0         4.2        1000  yes
sampling            6         0        10.2        1000  yes
control          12.5         0        22.7        5000  yes
telemetry          45         0        67.7       1e+06  yes
```

**Verificación contra el máximo observado (30 s de captura).** El R que predice el script es una *cota* de peor caso; el máximo observado en 30 s casi siempre queda por debajo, porque el peor caso (liberación simultánea de todas las tareas con el máximo $C$) es raro y puede no ocurrir en la ventana. Que la medición "casi" iguale la cota es lo esperado, no un fallo.

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu firmware/sched_labs -- -DCONFIG_SCHED_LAB_NOLOCK=y -DCONFIG_SCHED_LAB_RUN_S=30
```

| | RTA | Máximo observado (30 s) |
| :--- | :---: | :---: |
| $R_{control}$, sin lock | 1 000 µs | ⏳ ____ µs |

### Task B — La inversión, en vivo

`firmware/sched_labs/src/lab07.c`: tres hilos (`control` prio 2, `medium` prio 4, `slow` prio 6) liberados por `k_timer` una vez cada 100 ms con offsets 0 / 1 / 2 ms. En esta variante el lock es un **`k_sem` de 1** (sin herencia).

**Secuencia predicha** (t = 0 es la liberación de `slow`; resolución ±1 ms):

| Intervalo (ms) | Quién corre | Estado |
| :--- | :--- | :--- |
| 0 – 2 | `slow` | tiene el lock; D2 arriba. En t = 1, `control` lo pide y **se bloquea** (D3 arriba) |
| 2 – 12 | `medium` | **inversión:** desaloja a `slow`, que sigue con el lock; `control` espera (D1 arriba) |
| 12 – 14 | `slow` | termina su sección crítica y suelta el lock |
| 14 – 15 | `control` | por fin corre (D0 arriba hasta 15) |

$R_{control}$ = 15 − 1 = **14 ms** (modelo) frente al deadline de 5 ms.

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu firmware/sched_labs -- -DCONFIG_SCHED_LAB_INVERSION=y -DCONFIG_SCHED_LAB_RUN_S=30
west flash -r esp32
```

PulseView: canales D0 (control ocupado), D1 (medium ocupado), D2 (slow con el lock), D3 (control bloqueado); ≥ 100 kHz; disparo en el flanco de subida de D2. Resumen de consola a los 30 s:

```text
⏳ PEGAR AQUÍ el resumen real (max R control, max lock wait, max slow CS wall)
```

> **[CAPTURA PENDIENTE]** `labs/img/lab07_taskB_inversion.png` — captura con la inversión anotada (quién corre en cada segmento).
> <!-- ![Task B — inversión anotada](./img/lab07_taskB_inversion.png) -->
>
> **[CAPTURA PENDIENTE]** `labs/img/lab07_taskA_nolock.png` — la línea base sin lock.
> <!-- ![Baseline sin lock](./img/lab07_taskA_nolock.png) -->

### Task C — El arreglo

Mismo conjunto de tareas y misma secuencia; el lock pasa a ser un **`k_mutex`**.

**Secuencia predicha:** en t = 1, `control` se bloquea y `slow` hereda su prioridad; `medium` (prio 4) ya no puede desalojarla. `slow` termina su sección crítica en t = 4, `control` corre 4 – 5 y `medium` corre después (5 – 15). $R_{control}$ = 5 − 1 = **4 ms**.

**RTA extendido** ($R_i = C_i + B_i + $ interferencia, `rta.py --preset lab07-pi`):

```text
task                C         B           R           D  schedulable
control             1         4           5           5  yes
medium             10         0          11         100  yes
slow                4         0          15         100  yes
```

Para contraste, el modelo de la inversión (`--preset lab07-inversion`, `control` con la interferencia extra de `medium`):

```text
control            11         4          15           5  NO
```

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu firmware/sched_labs -- -DCONFIG_SCHED_LAB_PI=y -DCONFIG_SCHED_LAB_RUN_S=30
west flash -r esp32
```

```text
⏳ PEGAR AQUÍ el resumen real (esperado: max R control ≈ 4 ms, 0 misses)
```

> **[CAPTURA PENDIENTE]** `labs/img/lab07_taskC_pi.png` — la captura "después": `medium` ya no se interpone, D3 dura ≈ 3 ms.
> <!-- ![Task C — con herencia de prioridad](./img/lab07_taskC_pi.png) -->

**Contraste RTA vs. medición (a completar):**

| Escenario | RTA / modelo | Medido | ¿Coincide (±1 ms)? |
| :--- | :---: | :---: | :---: |
| Sin lock | 1 ms | ⏳ | ⏳ |
| `k_sem` (inversión) | 14 ms (cota 15) | ⏳ | ⏳ |
| `k_mutex` (PI) | 4 ms (cota 5) | ⏳ | ⏳ |

---

## §4 RTA del conjunto real y del escenario

### 4.1 El conjunto del nodo

Ver la salida de §3 Task A. Todas las tareas del nodo cumplen con amplio margen: el $R$ de `control` es **22.7 µs** frente a su deadline de **5 000 µs** (≈ 220 veces de holgura), con $B_i$ = 0 porque el nodo no tiene locks: su estado compartido es `atomic_t` (ADR-002, regla 3).

**Salvedades.** (1) Los $C_i$ son los del L476RG (Lab 02): hay que re-medirlos en el ESP32-S3, y `telemetry` allí bloquea ≈ 7 ms por el `printk` con polling. (2) El periodo de la ISR de flujo (1 ms) es un **supuesto**, no una medición. (3) El deadline de 5 ms se aplica al tiempo de reacción de `control`; la detección del e-stop en `sampling` (1 kHz) suma hasta 1 ms más antes de que `control` actúe.

### 4.2 Verificación cruzada con el Lab 06

El mismo script, sobre el conjunto de Task A del Lab 06 (RM, unidades de 10 ms):

```text
$ python3 tools/scripts/rta.py --tasks "tau1:1:5,tau2:3:6,tau3:4:15" --unit units --trace
  tau3: iterations R = 4 -> 8 -> 12 -> 13 -> 16
tau3                4         0          16          15  NO
```

$R_{\tau_3}$ = 16 > 15: el RTA exacto confirma lo que mostró la simulación (RM pierde `tau3`) y lo que los dos tests de utilización no podían decidir.

---

## §5 Checklist de evidencia pendiente

- [ ] Línea base sin lock: resumen de consola y `labs/img/lab07_taskA_nolock.png`
- [ ] `labs/img/lab07_taskB_inversion.png` con la inversión anotada, y su resumen de consola
- [ ] `labs/img/lab07_taskC_pi.png` y su resumen de consola
- [ ] Tablas de §1 y de contraste en §3 Task C completas con valores medidos
- [ ] ADR-002: pasar `Status` a *Accepted* y completar la fila "Medido" de §2
- [ ] Re-medir $C_i$ en el ESP32-S3 y reemplazar el conjunto del nodo en §3 Task A
