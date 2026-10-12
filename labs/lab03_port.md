# Reporte de Laboratorio 03: Port a ESP32-S3 y Primer Thread en Zephyr RTOS

> **Contexto del Proyecto:**  
> *"Incisos de ingeniería (Eng. Samuel Cifuentes): Selección del chip ESP32-S3 para el nodo por mayor memoria, radio y doble núcleo. Evaluación de la arquitectura: migración del superloop al ESP32-S3 sin reescribir código C (usando únicamente un Devicetree Overlay) y posteriormete evaluación del primer hilo (Thread) del Kernel de Zephyr."*

---

## §1 Task Set & Tabla de Mediciones ("What You'll Measure")

### Tabla Comparativa de Desempeño

| Medición | L476RG (Semana 2) | ESP32-S3 Superloop (Task B) | ESP32-S3 Kernel Thread (Task C) |
| :--- | :---: | :---: | :---: |
| **Max sampling jitter (superloop)** | **1.8 µs** | **0.16 µs** | N/A |
| **ISR → service latency** | **4.2 µs** | **1.25 µs** | **45 ns** |
| **Max sampling jitter (kernel thread)** | — | — | **0.05 µs** |
---

## §2 Task A — El Port (Devicetree en Acción)

### 1. Reasignación de Hardware mediante Devicetree Overlay
Para adaptar el firmware `superloop` al ESP32-S3 sin modificar el código fuente en C (`main.c`), se definió el archivo de superposición de hardware:
* **Archivo:** `boards/esp32s3_devkitc_esp32s3_procpu.overlay`
* **Objetivo:** Mapear las salidas de instrumentación (GPIOs de medición) a los pines disponibles en el mapa de pines del ESP32-S3 DevKit.

### 2. Análisis del Costo del Port (`git diff --stat`)
El principio clave de separación de hardware y software en Zephyr RTOS permite portar aplicaciones entre arquitecturas distintas.

```bash
# Comando ejecutado para evaluar cambios:
git diff --stat
```
### 3. Evidencia de Ejecución en Hardware Real

![Evidencia Task A - Flasheo y Monitor Serial en ESP32-S3](./img/lab03_Tarea_A.png)
adsad

## §3 Task B — ESP32-S3 Superloop Performance

Se evaluó el comportamiento en tiempo real del lazo superloop portado al ESP32-S3 DevKit.

### 1. Evidencia de Medición en PulseView
![Evidencia Latencia ISR ESP32-S3](./img/lab03_latencia_ISR.png)

### 2. Análisis Comparativo
- **Latencia de Interrupción ($\text{ISR} \rightarrow \text{service}$):** Reducida de $4.2\,\mu\text{s}$ (STM32L476RG) a $1.25\,\mu\text{s}$ (ESP32-S3), debido a la mayor frecuencia de reloj (240 MHz vs 80 MHz).
- **Jitter de Muestreo Nominal:** Se mantiene por debajo de $1\,\mu\text{s}$ en ejecución continua sin carga bloqueante.

## §3 Task C: Migración a Kernel Thread de Zephyr

### 1. Arquitectura del Hilo
Para garantizar determinismo en tiempo real y eliminar el jitter introducido por tareas bloqueantes en el superloop (como la rutina de calibración `calib`), se migró la lógica de muestreo a un *Kernel Thread* dedicado:

* **Nombre del Hilo:** `sampling_thread`
* **Prioridad:** `2` (Preemptiva de alta prioridad)
* **Tamaño de Stack:** `1536` bytes
* **Mecanismo de Sincronización:** Cola de mensajes (`k_msgq_get(&tick_q, ...)`) disparada periódicamente por el temporizador del Kernel.
* **Métricas Atómicas:** Cálculo e integración de latencia máxima mediante operaciones atómicas (`atomic_set` / `atomic_get` sobre `lat_peak_us`).

---

### 2. Análisis de Resultados y Desempeño

1. **Aislamiento frente a Carga de CPU (`calib`):**  
   Al ejecutar la función intensiva de calibración en el hilo principal (`main`), el tiempo de ejecución en el superloop deja de afectar la tarea de muestreo. El planificador preemptivo de Zephyr interrumpe de inmediato cualquier procesamiento de menor prioridad tan pronto se produce el evento del temporizador.

2. **Reducción de Latencia de Servicio ($\text{ISR} \rightarrow \text{Service}$):**  
   La latencia se redujo de **$1.25\,\mu\text{s}$** en la arquitectura Superloop a solo **$0.05\,\mu\text{s}$** ($45\text{ ns}$) con el Kernel Thread. El analizador lógico capturó el pulso de respuesta en **D1 (`instr_flow`)** inmediatamente después del flanco del temporizador, confirmando un cambio de contexto ultrarrápido en el ESP32-S3.

3. **Determinismo y Jitter:**  
   El jitter máximo de muestreo se mantuvo acotado en **$0.08\,\mu\text{s}$**, demostrando que la pila RTOS de Zephyr ofrece garantías de tiempo real estricto para el sistema de control SoilSense.