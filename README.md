# SAMPLO

**Looper / Sampler de audio en tiempo real construido sobre Daisy Seed.**

SAMPLO es un dispositivo de audio portátil que permite grabar, reproducir, sobreponer capas (overdub) y manipular loops en tiempo real con efectos y control visual mediante pantalla TFT.

Proyecto desarrollado por **Crearttech** y entregado al **CNA Centro Nacional de las Artes Delia Zapata Olivella** en Bogotá, Colombia.

---

## Características

- **Grabación y Reproducción** — Loop de hasta 10 segundos a 48kHz
- **Overdub** — Sobregrabar capas sobre el loop existente
- **Efectos en tiempo real** — Delay, Reverb, Pitch Shift, Filtros HP/LP
- **Reproducción reversa** — Inversión de la dirección de playback
- **Control de región** — Start/End point y movimiento del loop
- **Undo/Redo** — 3 niveles de historial
- **Pantalla TFT** — Visualización de waveform en tiempo real con interfaz de knobs circulares
- **Crossfade automático** — Transiciones suaves al cerrar el loop
- **Detección de jack** — Cambio automático entre entrada de línea y micrófono

## Hardware

| Componente | Descripción |
|---|---|
| Microcontrolador | Electro-Smith Daisy Seed (ARM Cortex-M7) |
| Pantalla | TFT ST7735 128×160 |
| Entrada de audio | ADC integrado del Daisy Seed |
| Salida de audio | DAC integrado del Daisy Seed |
| Controles | 4 Encoders rotativos con botón, 6 botones, LED RGB |

## Estructura del Código

```
SAMPLER_CNA/
├── SAMPLER_CNA.ino          # Programa principal (UI, controles, audio callback)
├── sampler_engine.h         # Motor de audio (grabación, playback, overdub, undo/redo)
├── sampler_effects.h        # Módulo de efectos (reverse, pitch shift, filtros)
├── sampler_dsp_utils.h      # Utilidades DSP optimizadas con CMSIS-DSP
├── sampler_state_machine.h  # Máquina de estados del looper
├── sampler_sync.h           # Sincronización de tempo y clock
└── sampler_hardware.h       # Mapeo de pines del Daisy Seed
```

## Instalación

1. Instalar [Arduino IDE](https://www.arduino.cc/en/software)
2. Instalar el board package **Electrosmith Daisy** desde el Board Manager
3. Instalar las bibliotecas:
   - `DaisyDuino`
   - `Adafruit GFX Library`
   - `Adafruit ST7735`
4. Abrir `SAMPLER_CNA/SAMPLER_CNA.ino`
5. Seleccionar board **Daisy Seed** y el puerto correspondiente
6. Compilar y subir

## Desarrolladores

**Crearttech**
- Andrés Melo
- Kelly Melo
- Rodrigo Aguilar

**En colaboración con DELIA**
- Javier Beltrán
- Santiago Gasca

**Crearttech** — Bogotá, Colombia, 2024

Proyecto entregado al **DELIA — Centro de Formación en Creación y Pedagogía de las Artes**, Bogotá, Colombia.

---

## Bibliografía y Referencias

### Hardware y Librerías Base
- **Electro-Smith**. (2020). *Daisy Seed Datasheet & Wiki*. [Daisy Seed Documentation](https://www.electro-smith.com/daisy/daisy).
- **DaisyDuino**. *Librería de soporte para el ecosistema Daisy en Arduino*. [GitHub Repository](https://github.com/electro-smith/DaisyDuino).
- **Adafruit Industries**. *Adafruit GFX Graphics Library*. [Documentation](https://learn.adafruit.com/adafruit-gfx-graphics-library).

### Procesamiento Digital de Señales (DSP)
- **Smith, S. W.** (1997). *The Scientist and Engineer's Guide to Digital Signal Processing*. California Technical Publishing.
- **Pirkle, W. C.** (2012). *Designing Audio Effect Plug-ins in C++*. Focal Press.
- **Zölzer, U.** (2011). *DAFX: Digital Audio Effects*. Wiley.

### Optimización y Algoritmos
- **ARM**. *CMSIS DSP Software Library Documentation*. [ARM Developer](https://arm-software.github.io/CMSIS_5/DSP/html/index.html).
- **D’Angelo, S., & Välimäki, V.** (2013). *Generalized Moog Ladder Filter*.

