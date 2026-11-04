# Guía de capturas — labs 05, 06 y 07

Qué hace cada quien: **yo** flasheo el firmware, reseteo la placa y mando comandos por la consola; **tú** armas PulseView, guardas los archivos y me avisas. Yo leo los archivos y relleno los informes. Nada se estima.

Ruta de `labs/img/` vista desde Windows: `\\wsl$\Ubuntu\home\xavi\4101137-real-time-systems\labs\img\`

## Protocolo de PulseView (común)

1. GND del analizador unido a GND de la placa. Canales según la tabla de cada captura.
2. Frecuencia de muestreo: la mayor que tu analizador permita con esos canales (≥ 12 MHz en el Lab 05; ≥ 100 kHz basta en los labs 06 y 07).
3. Muestras = ventana × frecuencia (ejemplo: 600 ms a 24 MHz = 14.4 M muestras).
4. Disparo: el de cada fila, con ≈ 10 % de pre-disparo.
5. Al terminar guarda **dos archivos con el mismo nombre** en `labs/img/`:
   - `nombre.png`: captura de pantalla con el zoom que muestre el hecho pedido.
   - `nombre.vcd`: *File → Export → VCD*. Solo guarda los cambios de nivel, así que pesa pocos KB y me permite calcular periodos y jitter exactos en vez de leerlos de la imagen.

## Lab 05 — en este orden

El firmware que está ahora en la placa es el definitivo con tracing activo.

| # | Archivo | Firmware | Canales | Ventana y disparo | Qué debe verse | Qué hago yo |
| :-: | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `lab05_taskB_gpio` | tracing ON (ya está) | D0 = GPIO4 (`sampling`), D2 = GPIO6 (`console`), D1 = GPIO15 (`flow`, plano si no hay pulsos) | 600 ms, disparo en subida de D2 | D2 alto ≈ 400 ms (el `calib`) y D0 sigue pulsando cada 1 ms dentro de ese tramo | Cuando me digas «armado», envío `calib` |
| 2 | `lab05_taskC_on` | tracing ON (ya está) | D0 = GPIO4 | ≥ 200 ms, disparo en subida de D0 | ≈ 200 pulsos de `sampling` a 1 kHz | Nada: operación normal |
| 3 | `lab05_taskC_off` | tracing OFF (`notrace.conf`) | D0 = GPIO4 | igual que la 2 | igual que la 2 | Compilo y flasheo antes; te aviso |

Con estos archivos relleno la latencia vista por GPIO, el jitter de muestreo de `sampling` con tracing ON y OFF, y el delta (el «precio de mirar»). Con solo D0 y D2 no se puede medir la latencia ISR→hilo, porque el firmware no tiene un pin en la ISR del tick; el informe lo dirá así.

## Lab 06 (guía oficial vigente)

Cableado: `firmware/sched_labs` usa D0 a D3 = GPIO4 a 7; `firmware/kernel` usa además D4 = GPIO21 (válvula) y D5 = GPIO18 (marcador del e-stop).

| Archivo | Firmware | Qué debe verse |
| :--- | :--- | :--- |
| `lab06_taskA_*` | `sched_labs` | Fig. 4.13 con τ1 (2, 5 ms) y τ2 (4, 7 ms): RM pierde `tau2` en t = 7 ms, EDF cumple 60 s (hoy son capturas del compañero de pareja) |
| `lab06_taskB_nolock` / `_inversion` / `_pi` | `sched_labs` con `-DCONFIG_SCHED_LAB_NOLOCK=y` / `INVERSION=y` / `PI=y` y `RUN_S=30` | D3 plano / D3 ≈ 13 ms (inversión) / D2 = B ≈ 4 ms |
| `lab06_taskC_estop_before` / `_after` (+ `_zoom`) | `kernel` antes y después de ADR-005 | detección (D5) → válvula baja (D4): hasta 10 ms antes, ≈ 4 µs después |

Sesión `.sr` de cada captura de la Task C en `labs/captures/`. Procedimiento: 3 MHz, 50 M muestras, sin disparo; tú pulsas Run al leer AHORA y la placa se resetea o dispara el e-stop unos segundos después.

## Lab 07

La inversión de prioridad se midió como Task B del Lab 06. Falta la guía oficial del Lab 07.
