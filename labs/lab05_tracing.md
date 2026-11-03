# Lab 05 / Week 5 — Tracing: ver lo que el analizador no ve

> **Contexto del Proyecto:**
> *"Eng. Samuel Cifuentes: el analizador lógico ve pines; necesito ver el **scheduler**: quién corrió, cuándo, por qué y quién esperó. Instrumenta el nodo con tracing y tráeme la primera traza con una preemption completa."*
>
> Guía técnica: [SOP-05](sops/sop05_tracing.md) · Firmware: `firmware/kernel/` · Placa: ESP32-S3 DevKitC (`esp32s3_devkitc/esp32s3/procpu`).

> **Estado de la evidencia:** el firmware está compilado y verificado en build, pero las capturas de hardware (traza CTF, PulseView) **aún no se toman**. Todo valor medido está marcado `⏳` y se completa con la captura correspondiente; ninguna cifra de esta tabla se estima ni se inventa.

---

## §1 Tabla de Mediciones ("What You'll Measure")

| Medición (ESP32-S3) | Valor según la traza (CTF) | Contraste con GPIO (Semana 4) |
| :--- | :---: | :---: |
| **Context switch (según traza)** | ⏳ ____ µs | 0.05 µs (45 ns) — ancho visible de cambio de contexto, [Lab 04](lab04_ipc.md) |
| **Latencia ISR → thread (según traza)** | ⏳ ____ µs | 0.05 µs (45 ns) — [Lab 04](lab04_ipc.md) |
| **Carga de CPU por hilo (operación normal)** | ⏳ ver §3 Task A (`threads`) | suma ≈ 100 % con `idle` |
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
```

### Consequences & Costs
* **Beneficio:** tracing completo sin comprar ni cablear nada; la consola queda intacta en el jack UART.
* **Costo:** hay que conectar **los dos** cables USB-C (UART para la consola, USB para la traza) y elegir el `tty` de cada uno explícitamente (no usar `head -n 1`).
* **Costo de CPU/RAM:** el modo activo es **asíncrono** (`CONFIG_TRACING_ASYNC=y`): un hilo de tracing propio (stack 1024 B) vacía un buffer circular de 2048 B. Ese hilo y el buffer son parte del "precio de mirar" (Task C).
* **Eventos habilitados:** `THREAD`, `ISR`, `SEMAPHORE`, `QUEUE` e `IDLE`.

---

## §3 Week-5 Evidence

### Task A — Habilitar el tracing

**Firmware (verificado en build):** el build base compila sin warnings (`FLASH: 136 612 B, 1.63 %`), y también con `pot_esp32.overlay` + `display.conf`. Los hilos reciben nombre legible en `start_threads()` mediante `k_thread_name_set` (`sampling`, `control`, `display`, `telemetry`, `console`) y el *workqueue* de flujo se llama `flow_wq`; con `CONFIG_THREAD_NAME=y` esos nombres aparecen en la traza y en `threads`.

**Captura (pasos del SOP-05, Ruta B adaptada):**

```bash
source ~/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null || true
west build -p auto -b esp32s3_devkitc/esp32s3/procpu firmware/kernel
west flash -r esp32

# Ambos USB-C conectados: el de la consola y el de la traza. Identificar cada tty:
ls -l /dev/ttyACM* /dev/ttyUSB*
timeout 60 cat /dev/ttyACMx > channel0_0          # ttyACMx = puerto USB-Serial-JTAG (traza)

mkdir trace && cp channel0_0 trace/
cp $ZEPHYR_BASE/subsys/tracing/ctf/tsdl/metadata trace/
babeltrace2 trace/ > trace.txt
```

**Estadísticas de ejecución por hilo** (`CONFIG_THREAD_RUNTIME_STATS=y`): desde la consola (UART) escribir `threads`. Cada línea trae `STACK: unused … usage … (N %); CPU: N %`. La suma de `CPU` de todos los hilos, incluido `idle`, debe aproximar 100 %.

```text
⏳ PEGAR AQUÍ la salida real de `threads` (operación normal, sin `calib`)
```

> **[CAPTURA PENDIENTE]** Traza abierta en el visor con los hilos nombrados. Guardar como `labs/img/lab05_taskA_trace.png` y activar la línea siguiente.
> <!-- ![Evidencia Task A — traza con hilos nombrados](./img/lab05_taskA_trace.png) -->

**Verificación esperada:** `trace.txt` muestra `thread_switched_in` / `thread_switched_out` con los nombres de los hilos y timestamps monótonos.

### Task B — La historia de una preemption

**Escenario.** Con la consola ejecutando `calib` (400 ms de `k_busy_wait` en el hilo `console`, prioridad 9, la más baja de la aplicación), llegan dos eventos de mayor prioridad:

| Cadena | Camino | Hilo que preempta |
| :--- | :--- | :--- |
| Tick de muestreo (1 kHz) | `tick_isr` (timer) → `k_msgq_put` en `tick_q` → despierta `sampling` | `sampling` (prio 2) sobre `console` (prio 9) |
| Pulsos de flujo | `flow_isr` → `k_work_submit_to_queue` → `flow_wq` | `flow_wq` (prio 5) sobre `console` (prio 9) |

Para el evento de flujo, inyectar ≥ 100 pulsos en GPIO16 (entrada `flow_pulse`) mientras corre `calib`; un lote completo dispara el trabajo diferido.

**Eventos a anotar** (tomados de `trace.txt`; la diferencia entre columnas es el valor de interés):

| # | Evento CTF | Qué significa | Timestamp (traza) |
| :---: | :--- | :--- | :---: |
| t0 | `isr_enter` | Entra la ISR del timer / de flujo | ⏳ ____ |
| t1 | `isr_exit` | Sale la ISR | ⏳ ____ |
| t2 | `thread_switched_out` (`console`) | El scheduler saca a `console` | ⏳ ____ |
| t3 | `thread_switched_in` (`sampling` / `flow_wq`) | Corre el hilo despertado | ⏳ ____ |

* **Latencia ISR → thread (traza)** = t3 − t0 = ⏳ ____ µs
* **Context switch (traza)** = t3 − t2 = ⏳ ____ µs
* **Duración de la ISR (traza)** = t1 − t0 = ⏳ ____ µs

> **[CAPTURA PENDIENTE]** Traza anotada (flechas sobre ISR → `k_msgq` → wake-up → preemption de `console`). Guardar como `labs/img/lab05_taskB_preemption.png`.
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

---

## §4 Análisis — Traza vs GPIO

> Las siguientes son **hipótesis de lectura** derivadas del código y de la configuración compilada; se confirman o se corrigen con las capturas de §3. No hay números medidos en esta sección.

**¿Por qué la traza y el GPIO no tienen por qué coincidir?** Observan puntos distintos de la misma historia:

1. **Punto de observación.** El evento `thread_switched_in` lo emite el *scheduler* en el instante del cambio de contexto. El flanco de D0 lo emite el *código del hilo*, ya dentro de `task_sampling()`, es decir, después del retorno de `k_msgq_get` y del cálculo de latencia. La latencia vista por GPIO incluye, por tanto, ese prólogo del hilo que la traza no cuenta.
2. **Resolución.** El timestamp CTF sale del contador de ciclos de la CPU (`CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=240000000`, ≈ 4.2 ns por ciclo). La resolución del GPIO la fija la frecuencia de muestreo del analizador lógico. Un tercer instrumento, `lat_peak_us` en `status`, usa `k_cyc_to_us_floor32` y **trunca a microsegundos enteros**: una latencia sub-microsegundo aparecería como `0`, así que no sirve para contrastar valores del orden de 0.05 µs.
3. **El observador perturba.** Cada evento CTF cuesta CPU dentro de la ruta que mide (ISR, cambio de contexto, `k_msgq_put`). El GPIO casi no perturba (una escritura de registro), por eso el curso mide los *números* con GPIO y explica la *historia* con la traza.

**Lectura del overhead (Task C).** El tracing activo suma tres costos que el delta de D0 agrupa: el formateo de eventos en el contexto que los genera, el hilo de tracing (`CONFIG_TRACING_THREAD_STACK_SIZE=1024`) que compite por CPU al vaciar el buffer, y el volumen de eventos (`ISR`, `QUEUE`, `SEMAPHORE`, `THREAD`, `IDLE`) a 1 kHz.

**Contingencia — eventos perdidos.** El buffer asíncrono es de 2048 B (`CONFIG_TRACING_BUFFER_SIZE`) y el hilo de tracing espera hasta 100 ms tras el primer paquete (`CONFIG_TRACING_THREAD_WAIT_THRESHOLD`). A 1 kHz de ticks el buffer podría llenarse antes de vaciarse, descartando paquetes. Si `babeltrace2` reporta errores o faltan eventos, probar en `prj.conf` (no validado en hardware):

```ini
CONFIG_TRACING_BUFFER_SIZE=8192
CONFIG_TRACING_THREAD_WAIT_THRESHOLD=20
```

---

## §5 Checklist de evidencia pendiente

- [ ] `labs/img/lab05_taskA_trace.png` — traza con hilos nombrados
- [ ] Salida real de `threads` pegada en §3 Task A
- [ ] `labs/img/lab05_taskB_preemption.png` — preemption anotada
- [ ] `labs/img/lab05_taskB_gpio.png` — D0/D1 en PulseView
- [ ] Tabla t0–t3 de Task B con timestamps reales
- [ ] `labs/img/lab05_taskC_off.png` y `lab05_taskC_on.png`
- [ ] Tabla §1 completa (4 filas) con los valores medidos
