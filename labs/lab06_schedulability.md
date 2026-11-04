# Lab 06 / Week 6 — Schedulability: la teoría puesta a prueba

> **Contexto del Proyecto:**
> *"Eng. Samuel Cifuentes: Daniela pregunta si puede agregar dos sensores más al nodo. Hoy respondemos como ingenieros: no 'probemos a ver', sino 'la utilización da X, el test dice Y'. Y como no confío en los teoremas más que en ti, reproduce el caso famoso: un conjunto de tareas que RM pierde y EDF cumple, en nuestra placa y no en el libro."*
>
> Referencia: Buttazzo, *Hard Real-Time Computing Systems* (4.ª ed.), cap. 4 · Firmware: `firmware/sched_labs/` y `firmware/kernel/` · Scripts: `tools/scripts/` · Placa: ESP32-S3 DevKitC.

> **Estado de la evidencia:** el firmware compila en ambas variantes (RM y EDF) y las **predicciones** de abajo salen de scripts reproducibles; las capturas del analizador y las mediciones de lazo **aún no se toman**. Todo valor medido está marcado `⏳`. Ninguna cifra medida se estima ni se inventa; los valores *predichos* están rotulados como tales.

---

## §1 Tabla de Mediciones ("What You'll Measure")

| Medición | Valor medido (placa) | Predicción |
| :--- | :---: | :---: |
| **U del conjunto sintético (3 tareas)** | 0.9667 (calculado) | diseñado a U ≈ 0.97 ✔ |
| **Deadline miss bajo RM (¿qué tarea, cuándo?)** | ⏳ ____ | sí: `tau3`, primer job, en t = 150 ms (simulación) |
| **Deadline miss bajo EDF** | ⏳ ____ | no: peor respuesta de `tau3` = 140 ms ≤ 150 ms (simulación) |
| **Error del lazo con jitter inducido de 0 / 2 / 5 ms** | ⏳ ____ / ____ / ____ mV | 12.2 / 12.2 / 12.4 mV (simulación, ver §3 Task C) |

---

## §2 Supuestos y decisiones

* **Parámetros de la Fig. 4.13.** Los periodos y cómputos del libro **no están en este repositorio**. Se usa un conjunto sintético de 3 tareas, de utilización similar (U = 0.967), cuyo comportamiento se **verificó por simulación**: RM falla y EDF cumple. Si la figura del libro trae otros números, se cambian en una sola tabla (`firmware/sched_labs/src/lab06.c` y el argumento `--tasks` de `sched_sim.py`).
* **Escala.** 1 unidad de tiempo del libro = 10 ms en la placa, para que los pulsos se vean con holgura en PulseView y el tick de 1 ms no distorsione.
* **Parámetros de Daniela.** Las dos tareas nuevas "se dan en clase" y no están en el repo: §4 deja el cálculo parametrizado y calcula cuánta utilización caben, sin inventar sus valores.
* **$C_i$ del nodo.** Los únicos $C_i$ registrados son los de la Semana 2 (L476RG, analizador lógico + PulseView). Los del ESP32-S3 se re-miden en el Lab 05.

---

## §3 Week-6 Evidence

### Task A — Fig. 4.13, en vivo

**Conjunto de tareas** (implementado en `firmware/sched_labs/src/lab06.c`, "cómputo" = espera activa calibrada `burn_ms`):

| Tarea | $C_i$ (ms) | $T_i = D_i$ (ms) | $U_i$ | Pin analizador | Prioridad RM |
| :--- | :---: | :---: | :---: | :---: | :---: |
| `tau1` | 10 | 50 | 0.200 | D0 (GPIO4) | 3 (más alta) |
| `tau2` | 30 | 60 | 0.500 | D1 (GPIO5) | 4 |
| `tau3` | 40 | 150 | 0.267 | D2 (GPIO6) | 5 |
| | | | **0.967** | D3 (GPIO7) = latch de miss | |

* **Liberación.** Cada tarea la libera un `k_timer` (ISR que estampa `k_cycle_get_32`), las tres dentro del mismo tick: liberación síncrona en t = 0, que es el instante crítico.
* **Pines.** D0–D2 suben en el primer despacho de un job y bajan al completarlo (con preemption, el pulso incluye el tiempo en que fue desalojado). D3 sube y **se queda arriba** en el primer deadline perdido.
* **RM:** prioridades fijas por periodo. **EDF:** una sola prioridad; al terminar cada job se fija con `k_thread_deadline_set` el deadline absoluto del siguiente (`CONFIG_SCHED_DEADLINE=y`).

**Predicción (simulación, `tools/scripts/sched_sim.py sim --timeline 30`, 1 carácter = 10 ms):**

```text
--- RM ---
DEADLINE MISS: tau3 job 1 released at t=0, deadline t=15, still has work left at t=15
      0    5    0    5    0    5
tau1  #....#....#...................
tau2  .###..###...###...............
tau3  ....#....#.#..................

--- EDF ---
no miss over 30 units
worst response (completed jobs): {'tau1': 2, 'tau2': 4, 'tau3': 14}
```

`tau3` recibe 1 unidad de CPU en [4,5), 1 en [9,10) y 1 en [11,12); `tau2` lo desaloja en t = 12 y, al llegar a t = 15, le falta 1 unidad: pierde su deadline. Bajo EDF, `tau3` termina en 14 unidades (140 ms).

**Cómo capturar:**

```bash
source ~/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null || true
# RM
west build -p always -b esp32s3_devkitc/esp32s3/procpu firmware/sched_labs
# EDF
west build -p always -b esp32s3_devkitc/esp32s3/procpu firmware/sched_labs -- -DCONFIG_SCHED_LAB_EDF=y
west flash -r esp32
```

PulseView: canales D0–D3, ≥ 100 kHz, ventana ≥ 600 ms (2 hiperperiodos), disparo en el flanco de subida de D0 justo después del reset. Consola (UART, 115200) al terminar los 20 s:

```text
-- RM (medido en la placa) --
sched_labs: Lab 06 Task A — rate-monotonic
burn_ms(10) took 9999 us with nothing else running
task   C[ms]  T[ms]   jobs  misses  dropped   worst[ms]
tau1      10     50    402       0        0      10.016
tau2      30     60    335       0        0      40.016
tau3      40    150    134      67        0     170.054
deadline missed: YES
scenario finished (0)

-- EDF (medido en la placa) --
sched_labs: Lab 06 Task A — EDF (SCHED_DEADLINE)
burn_ms(10) took 9999 us with nothing else running
task   C[ms]  T[ms]   jobs  misses  dropped   worst[ms]
tau1      10     50    402       0        0      40.087
tau2      30     60    335       0        0      40.067
tau3      40    150    134       0        0     140.103
deadline missed: no
scenario finished (0)
```

El resumen de RM se obtuvo dos veces (dos corridas de 20 s) con resultado idéntico.

![Task A — miss bajo RM](./img/lab06_taskA_rm_miss.png)

PulseView (Saleae Logic, 3 MHz, 50 M muestras, sin disparo; reset de la placa por RTS ≈ 8 s después de pulsar Run). Los cursores miden **149.997 ms** desde el primer pulso de `tau1` (D0, +9646 ms) hasta la subida de D3 (+9796 ms), que se queda arriba: el primer deadline perdido de `tau3`, a ≈ 150 ms como predijo la simulación. Los bloques anchos de D0–D3 antes de ≈ +9508 ms no son tareas: es el estado de los pines durante el arranque; en ≈ +9508 ms los cuatro bajan a la vez, que es lo que hace `instr_init()` (inferencia a partir del código, no medida aparte). La lectura de los cursores es de la imagen; no se exportó el VCD.

La peor respuesta medida de `tau3` (170.054 ms) es mayor que los 160 ms de la RTA exacta para el primer trabajo: se debe, probablemente, al retraso acumulado de los trabajos posteriores, hipótesis que no se verificó con los datos de la captura.

Bajo EDF el mismo conjunto cumple todos los deadlines: 0 fallos y la peor respuesta de `tau3` es **140.103 ms**, igual que los 140 ms de la simulación y por debajo de su deadline de 150 ms. Para `tau1` y `tau2` la peor respuesta sube respecto a RM (40.087 y 40.067 ms frente a 10.016 y 40.016 ms), porque EDF ya no les da prioridad fija; siguen dentro de sus deadlines.

![Task A — EDF cumple](./img/lab06_taskA_edf_ok.png)

PulseView con la misma configuración que RM (3 MHz, 50 M muestras, reset por RTS ≈ 8 s después de Run). El arranque, donde los cuatro canales bajan a la vez, queda en ≈ +5.6 s; desde ahí D0, D1 y D2 pulsan con regularidad y **D3 se queda abajo** en todo el tramo visible (≈ 4.5 s), sin la subida que en RM ocurría a ≈ 150 ms. El bloque alto de D3 al inicio es el estado de los pines durante el arranque, como en la captura de RM.

**Verificación de la calibración:** `main` imprime `burn_ms(10) took 9999 us with nothing else running` (medido); debe ser ≈ 10 000 µs, y lo es. Si se desvía, los $C_i$ efectivos no son los de la tabla.

### Task C — Jitter vs. control (el puente a teoría de control)

**Inyección.** `CONFIG_LAB06_JITTER_MAX_MS=N` hace que cada ciclo de control espere un número entero de ms pseudoaleatorio en [0, N] **antes** de escribir el duty de la válvula (PRNG xorshift32 de semilla fija: cada corrida repite los mismos retardos). La ruta de e-stop nunca se retrasa. N = 0 compila la inyección fuera: es el lazo original.

**Métrica.** `status` imprime `loop_err_mv`: el promedio de |setpoint − presión| sobre los ciclos de control desde el último `set <mv>`; `set` reinicia la ventana.

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu firmware/kernel -- -DCONFIG_LAB06_JITTER_MAX_MS=<N>   # N = 0, 2, 5
west flash -r esp32
# consola: esperar ~10 s tras el arranque (planta estable en 1500 mV), luego:
#   set 2000        <- escalón 1500 -> 2000 mV, abre la ventana
#   (esperar 10 s)
#   status          <- leer loop_err_mv
```

Usar la planta sintética (sin `pot_esp32.overlay`): con el potenciómetro la presión no responde al duty y el error no mide el lazo.

| N (ms) | `loop_err_mv` medido | Predicción (simulación) |
| :---: | :---: | :---: |
| 0 | ⏳ ____ mV | 12.2 mV |
| 2 | ⏳ ____ mV | 12.2 mV |
| 5 | ⏳ ____ mV | 12.4 mV |

**Predicción (`tools/scripts/loop_jitter_sim.py --sweep`, promedio de 8 semillas de ruido, ±0.5 mV):**

| N (ms) | 0 | 2 | 5 | 10 | 20 | 30 | 40 | 50 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| mean \|e\| (mV) | 12.0 | 11.7 | 12.3 | 12.1 | 12.2 | 14.5 | 16.5 | 20.9 |
| vs N = 0 | ×1.00 | ×0.98 | ×1.03 | ×1.01 | ×1.02 | ×1.21 | ×1.37 | ×1.75 |

*(Corrida de semilla única; en el promedio de 8 semillas: N=0 → 12.2, N=2 → 12.2, N=5 → 12.4, N=30 → 15.0, N=50 → 20.8 mV.)*

**Lectura de la predicción (hipótesis a confirmar con la placa).** Con esta planta (constante de tiempo ≈ 64 ms) y un periodo de control de 10 ms, un retardo de hasta 5 ms **no degrada de forma apreciable** el lazo: las diferencias están dentro del ruido de la simulación. El error residual de ≈ 12 mV lo domina la zona muerta del integrador (`error/8` trunca a 0 por debajo de 8 mV: sin ruido de planta la simulación converge a ≈ 7 mV) más el ruido de ±3 mV de la planta; no es jitter. La degradación se hace visible hacia N ≈ 30 ms (+21 %) y marcada en N = 50 ms (+75 %), cuando el retardo supera la constante de tiempo de la planta y el lazo empieza a oscilar (la desviación estándar del duty sube de 1.2 % a 2.5 %). Si la medición confirma esto, la respuesta a "¿a partir de qué N deja de ser útil el lazo?" es: **N ≈ 30 ms para esta planta**, no los 2–5 ms del enunciado.

> **[CAPTURA OPCIONAL]** `labs/img/lab06_taskC_ctrl_pulse.png` — `instr_ctrl` (GPIO5 del nodo `firmware/kernel`) con N = 5: el ancho del pulso de control varía entre ≈ 0 y 5 ms. Confirma que la inyección está activa.

---

## §4 Schedulability — Task B: ¿cabe la carga nueva?

### 4.1 El nodo real, con $C_i$ medidos

Instrumento de cada $C_i$: analizador lógico + PulseView (GPIO), L476RG superloop, Lab 02.

| Tarea | $C_i$ (µs) | $T_i$ (µs) | $U_i$ |
| :--- | :---: | :---: | :---: |
| Sampling | 6.0 | 1 000 | 0.006000 |
| Control | 12.5 | 10 000 | 0.001250 |
| Telemetry | 45.0 | 1 000 000 | 0.000045 |
| ISR de flujo | 4.2 | 1 000 *(supuesto: inter-arribo mínimo, no medido)* | 0.004200 |
| **U** | | | **0.011495** |

`python3 tools/scripts/sched_sim.py tests --preset node`

* **Liu & Layland** (n = 4): cota = 4(2^{1/4} − 1) = **0.7568** → U = 0.0115 ≤ cota ✔
* **Hiperbólico:** Π(U_i + 1) = **1.0115** ≤ 2 ✔
* **EDF:** U ≤ 1 ✔
* **Veredicto:** el nodo es planificable con RM; el test que lo respalda es Liu & Layland (suficiente y se cumple).

**Salvedades.** (1) Son $C_i$ del L476RG: en el ESP32-S3 `telemetry` imprime por UART *polled* y bloquea ≈ 7 ms a 115200 (comentario en `main.c`), así que su $C_i$ real será mucho mayor que 45 µs (U_tele ≈ 0.007); se re-mide. (2) Los tests de utilización ignoran el bloqueo (Lab 07) y el deadline de 5 ms del e-stop, que exige análisis de tiempo de respuesta.

### 4.2 "Las dos tareas de Daniela"

Sus parámetros no están en el repo. Con n = 6 tareas, la cota de Liu & Layland es 6(2^{1/6} − 1) = **0.7348**; con el nodo en U = 0.0115 queda un margen de **0.7233** de utilización:

```bash
python3 tools/scripts/sched_sim.py tests --preset node --room 2
python3 tools/scripts/sched_sim.py tests --preset node --extra "sensor_a:<C>:<T>,sensor_b:<C>:<T>"   # con sus C y T (µs)
```

| | $C$ (µs) | $T$ (µs) | $U$ |
| :--- | :---: | :---: | :---: |
| Sensor A | ⏳ ____ | ⏳ ____ | ⏳ ____ |
| Sensor B | ⏳ ____ | ⏳ ____ | ⏳ ____ |
| **U total (nodo + A + B)** | | | ⏳ ____ |

**Regla de decisión (ya calculada):** las dos tareas caben **si U_A + U_B ≤ 0.7233** (Liu & Layland, condición suficiente). Si el total supera 0.7348, el hiperbólico puede aún aprobarlo; si ambos fallan y U ≤ 1, el conjunto no está decidido y se resuelve con RTA (Lab 07).

### 4.3 Por qué los tests suficientes no bastan: el conjunto de Task A

`python3 tools/scripts/sched_sim.py tests --tasks "tau1:1:5,tau2:3:6,tau3:4:15"`:

| Test | Resultado |
| :--- | :---: |
| Liu & Layland (n = 3): U = 0.9667 vs cota 0.7798 | ✘ falla |
| Hiperbólico: Π = 2.280 vs 2 | ✘ falla |
| EDF: U ≤ 1 | ✔ |

Ambos tests suficientes para RM fallan, así que **no deciden**; la simulación exacta (Task A) muestra que RM efectivamente pierde `tau3`, y el RTA exacto del Lab 07 lo confirma: `python3 tools/scripts/rta.py --tasks "tau1:1:5,tau2:3:6,tau3:4:15" --unit units` converge a $R_{\tau_3}$ = 16 > $D$ = 15 unidades. Por eso RM no es la respuesta a U = 0.97 y EDF sí: es la lección de la figura del libro.

> **FreeRTOS.** Solo trae prioridades fijas (no hay EDF en el kernel): Task A no es reproducible tal cual. Con U = 0.97 la respuesta profesional es rediseñar el conjunto hasta que pase el test de RM (o bajar U).

---

## §5 Checklist de evidencia pendiente

- [x] `labs/img/lab06_taskA_rm_miss.png` y `labs/img/lab06_taskA_edf_ok.png`
- [x] Resúmenes de consola de RM y EDF pegados en §3 Task A
- [x] Línea `burn_ms(10) took … us` pegada (calibración): 9999 µs
- [ ] Tabla Task C con `loop_err_mv` medido para N = 0, 2, 5 y la frase final confirmada o corregida
- [ ] Parámetros de Daniela en §4.2 y U total
- [ ] §1 completa
