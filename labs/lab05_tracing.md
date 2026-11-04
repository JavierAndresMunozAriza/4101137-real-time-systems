# Lab 05 / Week 5 — Tracing: ver lo que el analizador no ve

> **Contexto del Proyecto:**
> *"Eng. Samuel Cifuentes: el analizador lógico ve pines; necesito ver el **scheduler**: quién corrió, cuándo, por qué y quién esperó. Instrumenta el nodo con tracing y tráeme la primera traza con una preemption completa."*
>
> Guía técnica: [SOP-05](sops/sop05_tracing.md) · Firmware: `firmware/kernel/` · Placa: ESP32-S3 DevKitC (`esp32s3_devkitc/esp32s3/procpu`).

> **Estado de la evidencia (2026-11-03):** el firmware corre en la ESP32-S3 y se capturaron la salida de `threads` y dos trazas CTF de 62 s (operación normal y con `calib`). Quedan pendientes lo que exige el **analizador lógico** (`taskB_gpio`, `taskC_off`, `taskC_on` y el jitter de la Task C) y las imágenes del visor de trazas. Los valores «según traza» salen de `tools/scripts/ctf_decode.py` y `ctf_preempt_stats.py` (ver la desviación del SOP en §3 Task A); todo lo no medido sigue como `⏳`.

---

## §1 Tabla de Mediciones ("What You'll Measure")

| Medición (ESP32-S3) | Valor según la traza (CTF) | Contraste con GPIO (Semana 4) |
| :--- | :---: | :---: |
| **Context switch (según traza)** | **4.71 µs** (mediana de t3 − t2, n = 1 796) | 0.05 µs (45 ns) — ancho visible de cambio de contexto, [Lab 04](lab04_ipc.md). **No comparable:** el valor de la traza incluye el costo de registrar los dos eventos (§4) |
| **Latencia ISR → thread (según traza)** | **20.2 µs** (mediana, `sampling` sobre `tracing_thread`, n = 1 698; p95 26.6 µs) · 19.4 µs sobre `idle` (n = 70) | 0.05 µs (45 ns) — [Lab 04](lab04_ipc.md). Mismo reparo |
| **Carga de CPU por hilo (operación normal)** | `tracing_thread` 39 %, `idle` 56 %, `sampling` 2 %, el resto 0 %; suma 97 % (§3 Task A) | suma ≈ 100 % con `idle` |
| **Overhead del tracing sobre el jitter de muestreo** | ⏳ ____ µs | jitter con tracing OFF ⏳ ____ µs vs ON ⏳ ____ µs (D0 vía GPIO) |

### Instrumento que midió cada $C_i$

A partir de esta semana cada $C_i$ lleva anotado su instrumento. Los únicos valores registrados hasta hoy son los de la Semana 2 (L476RG, superloop):

| Tarea | $C_i$ | Instrumento | Placa / Arquitectura |
| :--- | :---: | :--- | :--- |
| Sampling | 6.0 µs | Analizador lógico + PulseView (GPIO) | L476RG, superloop ([Lab 02](lab02_superloop.md)) |
| Control | 12.5 µs | Analizador lógico + PulseView (GPIO) | L476RG, superloop |
| Telemetry | 45.0 µs | Analizador lógico + PulseView (GPIO) | L476RG, superloop |
| ISR (flujo) | 4.2 µs | Analizador lógico + PulseView (GPIO) | L476RG, superloop |
| Sampling / Control / Telemetry en ESP32-S3 kernel | ⏳ | GPIO **y** traza CTF (re-medir) | ESP32-S3, kernel threads |

---

## §2 Decisión — Ruta de tracing: CTF sobre USB-Serial-JTAG

* **Status:** Accepted
* **Date:** 2026-10-29
* **Deciders:** Real-Time Systems Engineering Team

### Context
El SOP-05 ofrece dos rutas. La **Ruta A** (SystemView sobre RTT) exige una sonda SEGGER J-Link. La **Ruta B** (CTF sobre UART) asume un segundo adaptador USB-UART conectado a pines libres. El nodo dispone únicamente de la ESP32-S3 DevKitC: sin J-Link y sin segundo adaptador. La consola ya ocupa `uart0` (jack **UART**), y el tracing nunca debe compartirla (mezclaría bytes CTF con texto).

### Decision
Mantener la Ruta B pero enviar el stream CTF por el periférico **USB-Serial-JTAG** del chip (jack **USB**, segundo `/dev/ttyACM*`), sin hardware adicional:

```dts
/* firmware/kernel/boards/esp32s3_devkitc_esp32s3_procpu.overlay */
chosen {
    zephyr,tracing-uart = &usb_serial;
};
```

```ini
# firmware/kernel/prj.conf
CONFIG_THREAD_NAME=y
CONFIG_TRACING=y
CONFIG_TRACING_CTF=y
CONFIG_TRACING_BACKEND_UART=y
# más las otras dieciséis clases en "=n" (SYSCALL, WORK, MUTEX, ..., PM): ver prj.conf
```

### Consequences & Costs
* **Beneficio:** tracing completo sin comprar ni cablear nada; la consola queda intacta en el jack UART.
* **Costo:** hay que conectar **los dos** cables USB-C (UART para la consola, USB para la traza) y elegir el `tty` de cada uno explícitamente (no usar `head -n 1`).
* **Costo de CPU/RAM:** el modo activo es **asíncrono** (`CONFIG_TRACING_ASYNC=y`): un hilo de tracing propio (stack 1024 B) vacía un buffer circular de 2048 B. Ese hilo y el buffer son parte del "precio de mirar" (Task C).
* **Eventos habilitados:** `THREAD`, `ISR`, `SEMAPHORE`, `QUEUE` e `IDLE`. Zephyr activa todas las demás clases por defecto: la primera captura en hardware contenía 21 y se desactivaron las otras dieciséis para que el firmware coincida con esta lista (`.config` verificado, commit `e424c15`).

---

## §3 Week-5 Evidence

### Task A — Habilitar el tracing

**Firmware (verificado en build y en hardware):** el build base compila sin warnings (`FLASH: 136 596 B, 1.63 %`) y corre en la ESP32-S3; también compila con `pot_esp32.overlay` + `display.conf` (esa variante no se ejecutó en hardware). Los hilos reciben nombre legible en `start_threads()` mediante `k_thread_name_set` (`sampling`, `control`, `display`, `telemetry`, `console`) y el *workqueue* de flujo se llama `flow_wq`; con `CONFIG_THREAD_NAME=y` esos nombres aparecen en la traza y en `threads`.

**Incidente de la primera ejecución.** Con el stack por defecto, el firmware con tracing se detenía unos 2 s después de arrancar (`FATAL EXCEPTION`, `EXCCAUSE 28 load prohibited`, PC en `z_swap`, decodificado con `addr2line`). Causa: el hilo `thread_analyzer` necesita ≈ 1 232 B de stack con el tracing activo y tenía 1 024 B. Se fijó `CONFIG_THREAD_ANALYZER_AUTO_STACK_SIZE=2048` (commit `ea1255a`). La comparación en la placa fue: sin tracing estable; con tracing y 1 024 B se cae; con tracing y 2 048 B estable. Lección: «compila» no es «corre»; este firmware no se había ejecutado nunca en hardware.

**Captura (pasos del SOP-05, Ruta B adaptada):**

```bash
source ~/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null || true
west build -p auto -b esp32s3_devkitc/esp32s3/procpu firmware/kernel
west flash -r esp32 --esp-device /dev/ttyACM1   # puente UART; el USB-JTAG se reenumera en cada reset

# Ambos USB-C conectados: el de la consola y el de la traza. Identificar cada tty:
ls -l /dev/ttyACM* /dev/ttyUSB*
timeout 60 cat /dev/ttyACMx > channel0_0          # ttyACMx = puerto USB-Serial-JTAG (traza)

mkdir trace && cp channel0_0 trace/
cp $ZEPHYR_BASE/subsys/tracing/ctf/tsdl/metadata trace/
babeltrace2 trace/ > trace.txt
```

**Desviación del SOP — decodificación.** `babeltrace2 2.1.2` aborta antes de emitir una sola línea (`Babeltrace 2 library precondition not satisfied … message-packet-beginning-create-with-default-clock-snapshot`), también con una copia del `metadata` a la que se añadió un reloj. Se decodificó con `tools/scripts/ctf_decode.py`, que lee las mismas definiciones de evento de `tsdl/metadata` (cabecera `u64 timestamp [ns] + u16 id`, campos de tamaño fijo) y se resincroniza si faltan bytes:

```bash
python3 tools/scripts/ctf_decode.py $ZEPHYR_BASE/subsys/tracing/ctf/tsdl/metadata channel0_0 > trace.txt
python3 tools/scripts/ctf_preempt_stats.py trace.txt          # despertares de sampling: t0..t3
```

Captura A final: 12 582 eventos y 31 B descartados de 299 988 (dos resincronizaciones: un evento dañado de 17 B y el último, cortado al terminar `timeout`). **La captura se tomó sin resetear la placa:** cada reset reenumera el USB-JTAG y `ttyACM0` desaparece de WSL unos segundos, y con ello se corta el `cat`.

**Estadísticas de ejecución por hilo** (`CONFIG_THREAD_RUNTIME_STATS=y`): desde la consola (UART) escribir `threads`. Cada línea trae `STACK: unused … usage … (N %); CPU: N %`. La suma de `CPU` de todos los hilos, incluido `idle`, debe aproximar 100 %.

```text
Thread analyze:
 flow_wq             : STACK: unused  592 usage  432 / 1024 ( 42 %); CPU:   0 %
                     : Total CPU cycles used: 5883
 thread_analyzer     : STACK: unused  816 usage 1232 / 2048 ( 60 %); CPU:   0 %
                     : Total CPU cycles used: 56954143
 telemetry           : STACK: unused 1408 usage  640 / 2048 ( 31 %); CPU:   0 %
                     : Total CPU cycles used: 1534147
 sampling            : STACK: unused  960 usage  576 / 1536 ( 37 %); CPU:   2 %
                     : Total CPU cycles used: 163160427
 display             : STACK: unused 1536 usage  512 / 2048 ( 25 %); CPU:   0 %
                     : Total CPU cycles used: 1916325
 control             : STACK: unused  464 usage  560 / 1024 ( 54 %); CPU:   0 %
                     : Total CPU cycles used: 17820668
 console             : STACK: unused 1424 usage  624 / 2048 ( 30 %); CPU:   0 %
                     : Total CPU cycles used: 19718016
 tracing_thread      : STACK: unused  448 usage  576 / 1024 ( 56 %); CPU:  39 %
                     : Total CPU cycles used: 2483970917
 sysworkq            : STACK: unused 1744 usage  304 / 2048 ( 14 %); CPU:   0 %
                     : Total CPU cycles used: 3180
 idle                : STACK: unused    0 usage  256 /  256 (100 %); CPU:  56 %
                     : Total CPU cycles used: 3524486412
 ISR0                : STACK: unused 1472 usage  576 / 2048 ( 28 %); CPU:   0 %
                     : Total CPU cycles used: 0
```

Salida real de `threads` (operación normal, sin `calib`, host leyendo la traza). Suma de `CPU` = 0 + 0 + 0 + 2 + 0 + 0 + 0 + 39 + 0 + 56 + 0 = **97 %** (redondeos del analizador). Dos lecturas:

* El hilo de tracing (`tracing_thread`, el que vacía el buffer por USB) consume el **39 %** de la CPU: es la primera cifra del «precio de mirar» (Task C), aún sin el jitter de D0.
* `idle` figura con 0 B libres de 256 B (100 %) con el tracing activo; con el tracing apagado usó 96 B. Subir `CONFIG_IDLE_STACK_SIZE` a 512 B no cambió la lectura (100 % de 512 B), así que **no se interpreta como un desborde confirmado**: queda como pregunta abierta y no se modificó el firmware por ello.

> **[CAPTURA PENDIENTE]** Traza abierta en el visor con los hilos nombrados. Guardar como `labs/img/lab05_taskA_trace.png` y activar la línea siguiente.
> <!-- ![Evidencia Task A — traza con hilos nombrados](./img/lab05_taskA_trace.png) -->

**Verificación:** `trace.txt` (captura A final) contiene `thread_switched_in` con los nombres `sampling` (908), `console` (157), `control` (72), `idle` (40), `thread_analyzer` (17) y `display` (16). Los timestamps crecen dentro de cada segmento de reloj, pero esto es un criterio de aceptación del decodificador y no una comprobación independiente; el salto del reloj a los ≈ 76.8 s se marca aparte (ver §4). `tracing_thread` aparece como hilo desplazado, no con `thread_switched_in` propio en esta captura.

### Task B — La historia de una preemption

**Escenario.** Con la consola ejecutando `calib` (400 ms de `k_busy_wait` en el hilo `console`, prioridad 9, la más baja de la aplicación), llegan dos eventos de mayor prioridad:

| Cadena | Camino | Hilo que preempta |
| :--- | :--- | :--- |
| Tick de muestreo (1 kHz) | `tick_isr` (timer) → `k_msgq_put` en `tick_q` → despierta `sampling` | `sampling` (prio 2) sobre `console` (prio 9) |
| Pulsos de flujo | `flow_isr` → `k_work_submit_to_queue` → `flow_wq` | `flow_wq` (prio 5) sobre `console` (prio 9) |

Para el evento de flujo, inyectar ≥ 100 pulsos en GPIO16 (entrada `flow_pulse`) mientras corre `calib`; un lote completo dispara el trabajo diferido.

**Resultado: el escenario de la consola no se pudo observar en la traza.** Se tomaron dos capturas de 62 s con `calib` enviado a los 12, 30 y 48 s. La consola confirma que las tres `calib` se ejecutaron (`calibration done` ×3), pero **ninguna aparece en la traza**: el tramo continuo más largo del hilo `console` registrado dura 0.014 ms (149 intervalos por captura), frente a los 400 ms de `calib`. La traza es una muestra con pérdidas: en 62 s solo se registraron ≈ 1 100 `isr_enter` de los ≈ 62 000 ticks (1.8 %) a ≈ 4.8 KB/s (§4). Tampoco se midió el camino `flow_isr` → `flow_wq`: no hay fuente de pulsos para GPIO16. La preemption de `console` por `sampling` queda para la captura de GPIO (D0).

**Lo que sí quedó íntegro** son 1 796 despertares de `sampling` (cadena contigua `isr_enter` → `thread_sched_ready` → `thread_switched_out` → `thread_switched_in`): 1 698 sobre `tracing_thread`, 70 sobre `idle` y 28 sobre `thread_analyzer`. Ese es el mismo mecanismo (ISR del timer → `k_msgq_put` → despertar → cambio de contexto) con otro hilo desplazado. El de latencia mediana sobre `idle`, tomado de `trace.txt`:

| # | Evento CTF | Qué significa | Desde t0 (traza) |
| :---: | :--- | :--- | :---: |
| t0 | `isr_enter` | Entra la ISR del timer de muestreo (reloj CTF 16.134000770 s) | 0.00 µs |
| — | `thread_sched_ready` (`sampling`) | `k_msgq_put` deja listo a `sampling` | +7.26 µs |
| t1 | `isr_exit` | **No se registra** (0 `isr_exit` en 2 200 `isr_enter`) | n/d |
| t2 | `thread_switched_out` (`idle`) | El scheduler saca a `idle` | +14.61 µs |
| t3 | `thread_switched_in` (`sampling`) | Corre el hilo despertado | +19.38 µs |
| — | `gpio_port_set_bits_raw_enter` (D0, GPIO4) | `sampling` sube D0 | +23.05 µs |

* **Latencia ISR → thread (traza)** = t3 − t0 = 19.38 µs
* **Context switch (traza)** = t3 − t2 = 4.77 µs
* **Duración de la ISR (traza)** = t1 − t0: **no medible** con esta traza, porque el kernel no emite `isr_exit` en esta ruta

Estadísticas de los 1 796 despertares (captura A final + B final, µs):

| Hilo desplazado | n | ISR → thread: mediana / p95 / máx | t3 − t2: mediana | t3 → escritura de D0: mediana |
| :--- | :---: | :---: | :---: | :---: |
| `tracing_thread` | 1 698 | 20.16 / 26.57 / 33.00 | 4.71 | 3.67 |
| `idle` | 70 | 19.37 / 25.68 / 25.90 | 4.71 | 3.67 |
| `thread_analyzer` | 28 | 20.25 / 26.64 / 26.68 | 4.73 | 3.68 |

Una escritura de D0 aparece 9.0 ms después de su `thread_switched_in` (valor máximo de la última columna para `tracing_thread`); es un caso aislado y no se usa.

> **[CAPTURA PENDIENTE]** Traza anotada en el visor (flechas sobre ISR → `k_msgq` → wake-up → cambio de contexto), con el despertar sobre `idle` de la tabla anterior, porque `console` no quedó registrado. Guardar como `labs/img/lab05_taskB_preemption.png`. Mientras tanto la evidencia es el extracto textual de la tabla.
> <!-- ![Task B — preemption anotada](./img/lab05_taskB_preemption.png) -->

**Contraste con GPIO.** D0 (`instr_samp`, GPIO4) sube al *entrar* a `task_sampling`; D1 (`instr_flow`, GPIO15) marca el cálculo por lotes en `flow_wq`. La ISR de flujo **no** tiene pin propio (es un único `atomic_inc`): su duración solo se lee en la traza (`isr_enter` → `isr_exit`). Captura de PulseView con D0 y D1 durante el mismo escenario:

> **[CAPTURA PENDIENTE]** `labs/img/lab05_taskB_gpio.png` — latencia vista por GPIO: ⏳ ____ µs.

### Task C — El precio de mirar

El tracing también es carga. Se mide el jitter de muestreo en **D0 (GPIO4)** con el protocolo habitual de PulseView, dos veces, con exactamente el mismo firmware salvo el tracing:

```bash
# Tracing ON  (configuración por defecto)
west build -p auto   -b esp32s3_devkitc/esp32s3/procpu firmware/kernel
# Tracing OFF (firmware/kernel/notrace.conf: CONFIG_TRACING=n)
west build -p always -b esp32s3_devkitc/esp32s3/procpu firmware/kernel -- -DEXTRA_CONF_FILE=notrace.conf
```

> `notrace.conf` verificado: el `.config` resultante queda con `# CONFIG_TRACING is not set`. Kconfig emite dos warnings *"choice symbol … was selected"* porque `prj.conf` conserva las opciones CTF/UART; son inocuos.

| Configuración | Max sampling jitter (D0) | Captura |
| :--- | :---: | :--- |
| Tracing **OFF** | ⏳ ____ µs | `labs/img/lab05_taskC_off.png` |
| Tracing **ON** | ⏳ ____ µs | `labs/img/lab05_taskC_on.png` |
| **Delta (overhead)** | ⏳ ____ µs | |

Referencia de contexto: el jitter nominal del kernel medido en la Semana 4 (antes de habilitar el tracing) fue de 0.08 µs ([Lab 04](lab04_ipc.md)); el delta de esta tabla se calcula contra la medición de hoy, no contra ese valor.

Indicador preliminar del costo, ya medido y sin sustituir al jitter de D0: con el tracing activo el hilo `tracing_thread` consumió el **39 %** de la CPU (`threads`, §3 Task A), y `idle` quedó en 56 %.

---

## §4 Análisis — Traza vs GPIO

> Los puntos 1 y 2 siguen siendo **hipótesis de lectura** derivadas del código y de la configuración compilada; se confirman o se corrigen con la captura de GPIO. Los puntos 3 a 5 y la contingencia ya tienen datos de las trazas de §3.

**¿Por qué la traza y el GPIO no tienen por qué coincidir?** Observan puntos distintos de la misma historia:

1. **Punto de observación.** El evento `thread_switched_in` lo emite el *scheduler* en el instante del cambio de contexto. El flanco de D0 lo emite el *código del hilo*, ya dentro de `task_sampling()`, es decir, después del retorno de `k_msgq_get` y del cálculo de latencia. La latencia vista por GPIO incluye, por tanto, ese prólogo del hilo que la traza no cuenta.
2. **Resolución.** El timestamp CTF sale del contador de ciclos de la CPU (`CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=240000000`, ≈ 4.2 ns por ciclo). La resolución del GPIO la fija la frecuencia de muestreo del analizador lógico. Un tercer instrumento, `lat_peak_us` en `status`, usa `k_cyc_to_us_floor32` y **trunca a microsegundos enteros**: una latencia sub-microsegundo aparecería como `0`, así que no sirve para contrastar valores del orden de 0.05 µs.
3. **El observador perturba.** Cada evento CTF cuesta CPU dentro de la ruta que mide (ISR, cambio de contexto, `k_msgq_put`). El GPIO casi no perturba (una escritura de registro), por eso el curso mide los *números* con GPIO y explica la *historia* con la traza.

   **Medido:** los eventos consecutivos de la traza se separan unos 3 a 7 µs, y el cambio de contexto según traza (4.71 µs) es ≈ 100 veces el de 45 ns por GPIO del Lab 04. Esa diferencia no se puede separar del costo de registrar los dos eventos, así que los números de la traza solo sirven para el orden y la forma de la historia.
4. **El reloj del CTF se envuelve.** `ctf_top_timestamp_get()` usa `timing_ns_get()`. Los timestamps vuelven a 0 cada ≈ 76.86 s: se observó 76.861431531 s → 0.000000507 s en la primera captura y 76.764038 s → 0.209567 s en la A final. El valor coincide con 2⁶⁴ / (10⁹ × 240 MHz) = 76.8614336 s, lo que apunta a que la conversión de ciclos a ns desborda 64 bits (inferencia numérica: no se leyó el código de la conversión). Un intervalo que cruce el salto sale negativo o enorme; el decodificador lo marca con una línea `# --- timestamp discontinuity ---` y las estadísticas se calculan dentro de cada segmento.
5. **La traza es una muestra, no un registro completo.** Las configuraciones probadas en la placa, con 62 s de captura cada una:

   | Configuración | Bytes | Ritmo | `isr_enter` registrados | `tracing_thread` CPU † |
   | :--- | :---: | :---: | :---: | :---: |
   | 21 clases, buffer 2 KB, umbral 100 ms (primera captura) | 702 051 | 11.3 KB/s | 1 962 (3.2 %) | 3 % ‡ |
   | 5 clases, buffer 16 KB, umbral 5 ms | 304 339 | 4.9 KB/s | 906 (1.5 %) | 75 % |
   | 5 clases, buffer 16 KB, umbral 100 ms | 304 374 | 4.9 KB/s | 910 (1.5 %) | 61 % |
   | **5 clases, buffer 2 KB, umbral 100 ms (final)** | 299 988 | 4.8 KB/s | 1 109 (1.8 %) | 39 % |

   † Porcentaje acumulado que reporta el analizador. ‡ La placa llevaba 47 min encendida y el host solo conectado al final: no es comparable con las otras filas. El mayor ritmo de la primera captura queda **sin explicar**.

   Con buffer de 16 KB las discontinuidades caen cada ≈ 16 KB: el buffer se llena en ≈ 0.1–0.3 s en una ráfaga contigua y el hilo de tracing tarda ≈ 3 s en vaciarlo, durante los cuales se pierden los eventos nuevos. Un umbral de 5 ms no sube el flujo y lleva al hilo de tracing al 75 % de la CPU. La causa de fondo del tope de ≈ 5 KB/s no se determinó (el hilo de tracing, el USB-Serial-JTAG o el reenvío `usbipd`).

**Lectura del overhead (Task C).** El tracing activo suma tres costos que el delta de D0 agrupa: el formateo de eventos en el contexto que los genera, el hilo de tracing (`CONFIG_TRACING_THREAD_STACK_SIZE=1024`) que compite por CPU al vaciar el buffer, y el volumen de eventos (`ISR`, `QUEUE`, `SEMAPHORE`, `THREAD`, `IDLE`) a 1 kHz.

**Contingencia — eventos perdidos (confirmada en hardware).** El buffer asíncrono es de 2048 B (`CONFIG_TRACING_BUFFER_SIZE`) y el hilo de tracing espera hasta 100 ms tras el primer paquete (`CONFIG_TRACING_THREAD_WAIT_THRESHOLD`). A 1 kHz de ticks el buffer se llena antes de vaciarse y se descartan paquetes: solo el 1.8 % de los ticks quedó registrado. Se probó un buffer de 16 KB con umbral de 5 ms y de 100 ms (tabla del punto 5): **no recupera eventos** (la cobertura fue menor que con 2 KB) y con 5 ms el hilo de tracing sube al 75 % de la CPU. Los valores 8192 / 20 ms propuestos antes no se probaron literalmente. Para ver una preemption completa hace falta reducir el volumen de eventos (por ejemplo, un tick más lento durante la captura, no probado) o apoyarse en el GPIO en lugar de la traza.

---

## §5 Checklist de evidencia pendiente

- [ ] `labs/img/lab05_taskA_trace.png` — traza con hilos nombrados (requiere un visor de trazas)
- [x] Salida real de `threads` pegada en §3 Task A
- [ ] `labs/img/lab05_taskB_preemption.png` — preemption anotada (requiere un visor de trazas)
- [ ] `labs/img/lab05_taskB_gpio.png` — D0/D1 en PulseView (requiere analizador lógico; D1 y el camino de flujo necesitan además pulsos en GPIO16)
- [x] Tabla t0–t3 de Task B con timestamps reales — parcial: t0, t2 y t3 medidos sobre `idle`; t1 (`isr_exit`) no se emite; la preemption de `console` no quedó registrada
- [ ] `labs/img/lab05_taskC_off.png` y `lab05_taskC_on.png` (requiere analizador lógico)
- [ ] Tabla §1 completa (4 filas) con los valores medidos — 3 de 4 filas; falta el jitter de la Task C
