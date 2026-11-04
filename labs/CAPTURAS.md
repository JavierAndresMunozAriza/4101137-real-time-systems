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

## Lab 06 — `firmware/sched_labs` (D0–D3 = GPIO4–7)

| Archivo | Firmware | Qué debe verse |
| :--- | :--- | :--- |
| `lab06_taskA_rm_miss` | RM (por defecto) | D3 sube cuando `tau3` pierde su deadline (a ≈ 150 ms) |
| `lab06_taskA_edf_ok` | EDF (`-DCONFIG_SCHED_LAB_EDF=y`) | D3 siempre abajo |
| `lab06_taskC_ctrl_pulse` (opcional) | `firmware/kernel` con `-DCONFIG_LAB06_JITTER_MAX_MS=5` | ancho del pulso de GPIO5 entre ≈ 0 y 5 ms |

Ventana ≥ 600 ms, ≥ 100 kHz, disparo en subida de D0. Para que el disparo coincida con el arranque, **tú armas y yo reseteo la placa**.

## Lab 07 — `firmware/sched_labs` (D0–D3 = GPIO4–7)

| Archivo | Firmware | Qué debe verse |
| :--- | :--- | :--- |
| `lab07_taskA_nolock` | `-DCONFIG_SCHED_LAB_NOLOCK=y` | `control` responde en ≈ 1 ms |
| `lab07_taskB_inversion` | `-DCONFIG_SCHED_LAB_INVERSION=y` | inversión de prioridad: D3 (`control` bloqueado) ≈ 14 ms |
| `lab07_taskC_pi` | `-DCONFIG_SCHED_LAB_PI=y` | con herencia de prioridad D3 ≈ 3–4 ms |

Canales: D0 `control` ocupado, D1 `medium`, D2 `slow` con el lock, D3 `control` bloqueado. ≥ 100 kHz, disparo en subida de D2.

Los valores esperados son **predicciones** (ver cada informe); lo que se mida las confirma o las corrige.
