# Reporte de Laboratorio 01: Bring-up, Entorno y Tabla RET

## §1 Información del Equipo y Plataforma
* **Integrantes:** Javier Andrés Muñoz Ariza
* **Tarjeta de Desarrollo:** NUCLEO-L476RG / NUCLEO-G474RE
* **Toolchain & RTOS:** Zephyr RTOS v3.x | West | OpenOCD | WSL2 (Ubuntu)

---

## §2 Tabla de Requerimientos y Ejecución (RET) — Task Set Inicial

| Task ID | Task Name | Period ($T_i$) | Deadline ($D_i$) | Execution Time ($C_i$) | Type (H / F / S) | Description |
| :---: | :--- | :---: | :---: | :---: | :---: | :--- |
| **TSK-01** | Sampling Loop | $1000\ \mu\text{s}$ | $1000\ \mu\text{s}$ | **TBD** *(medido en Lab 02)* | **Hard (H)** | Muestreo periódico crítico del sistema. |
| **TSK-02** | Control Loop | $10\text{ ms}$ | $10\text{ ms}$ | **TBD** *(medido en Lab 02)* | **Hard (H)** | Algoritmo de control de irrigación y válvulas. |
| **TSK-03** | Flow ISR Service | Event-driven | $10\ \mu\text{s}$ | **TBD** *(medido en Lab 02)* | **Hard (H)** | Atención inmediata por interrupción de caudal. |
| **TSK-04** | Telemetry Transmit | $100\text{ ms}$ | $100\text{ ms}$ | **TBD** *(medido en Lab 02)* | **Soft (S)** | Envío de datos síncronos/asíncronos. |
| **TSK-05** | Console / Shell | Background | Best-effort | **TBD** | **Soft (S)** | Interfaz CLI interactiva con el usuario. |

*Nota: La clasificación H (Hard) requiere cumplimiento estricto del deadline; S (Soft) tolera retrasos ocasionales sin falla catastrófica.*

---

## §3 Evidencias de Ejecución y Ciclo de Iteración

### Task A — Blinky & Hardware Bring-up
* **Resultado:** Verificación exitosa de compilación y flasheo sobre hardware STM32 mediante OpenOCD.
* **Comando de compilación:**
```text
west build -p auto -b nucleo_l476rg zephyr/samples/basic/blinky && west flash -r openocd 
  ```
### Task B — Monitor Serial & Modificación de Mensaje
* **Resultado:** Verificación del ciclo completo de iteración (modificación de código $\rightarrow$ rebuild $\rightarrow$ flash $\rightarrow$ serial monitor).
* **Consola Serial (115200 8N1 via tio):**
*** Booting Zephyr OS build v3.x.x ***
```text
 Hello World! SoilSense Bring-up verified by Javier Muñoz
 Board: nucleo_l476rg | Iteration cycle < 1 min DEMONSTRATED.
```
 ## §4 Automatización e Infraestructura Out-of-Tree Desarrollada
Como mejora técnica al proceso de Bring-up, se desarrolló un repositorio de infraestructura independiente para automatización:
* Repositorio de automatización: JavierAndresMunozAriza/lab01_bringup
* Aportes clave desarrollados:
  * Out-of-Tree Workflow: Separación limpia entre el código de aplicación y el código fuente de Zephyr.
  * Abstracción Devicetree: Uso de alias portables (led0, sw0) para ejecutar el mismo binario en STM32 y ESP32-C6.
  * Automatización en VS Code: Integración de atajos de teclado **(.vscode/tasks.json)** para compilación y monitoreo serial automático con **tio /dev/ttyACM0**.
  
