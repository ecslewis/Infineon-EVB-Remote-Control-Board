# Infineon EVB Remote Control Board
![Status](https://img.shields.io/badge/status-active-brightgreen)
![Platform](https://img.shields.io/badge/platform-dsPIC-blue)
![Language](https://img.shields.io/badge/language-C%20%7C%20Python-orange)
![Comms](https://img.shields.io/badge/comms-RS--485-yellow)

> Firmware and host interface for remote control of the Infineon 900V Bridge Driver Stage (BDS)
> Evaluation Board. Designed for power electronics research, converter development, and
> ZVS switching characterisation.

---

## Table of Contents

- [Overview](#overview)
- [Getting Started](#getting-started)
- [Hardware Requirements](#hardware-requirements)
- [Wiring / Connections](#wiring--connections)
- [System Architecture](#system-architecture)
- [PWM Control Modes](#pwm-control-modes)
- [Features](#features)
- [How It Works](#how-it-works)
- [Project Structure](#project-structure)
- [Known Limitations](#known-limitations)
- [Notes](#notes)

---

## Overview

This project provides embedded firmware running on a **dsPIC microcontroller** paired with
a **Python-based host interface** for full remote control of the
**Infineon 900V Bridge Driver Stage Evaluation Board**.

The system is designed around high-frequency power conversion research, enabling engineers
to remotely configure, tune and monitor switching behaviour without physical access to the
board. Communication is handled over an **RS-485 differential serial bus**, providing
noise-immune transmission suitable for high EMI environments typical of power electronics labs.

The firmware handles all real-time critical tasks — PWM generation, dead-time insertion,
special event triggering and RDSon measurement cycles — while the Python host provides a
clean command interface for frequency sweeps, duty cycle changes and mode switching.

Supported control platforms:
- ⚡ **ACZVS** – AC Zero Voltage Switching
- ⚡ **DCZVS** – DC Zero Voltage Switching
- ⚡ **DyR**   – Dynamic Rectification

> Compatible with **unidirectional devices only**

---

## Getting Started

### 1. Connect Hardware
Ensure all hardware is connected and powered before launching the interface.
See [Hardware Requirements](#hardware-requirements) and [Wiring](#wiring--connections) below.

### 2. Power Up Sequence
```
1. Connect RS-485 cable between host PC and MCU control board
2. Apply 12V DC to the MCU control board
3. Apply gate drive power to the BDS EVB
4. Confirm LED heartbeat is blinking on MCU board
5. Launch Python interface
```

### 3. Launch Host Interface
```bash
python main.py
```

### 4. Send Commands
Once connected, the interface will prompt for commands:
```
> SET_FREQ 100000       # set switching frequency to 100kHz
> SET_DUTY 50           # set duty cycle to 50%
> SET_MODE DCZVS        # switch to DCZVS mode
> RDSON                 # trigger RDSon measurement cycle
```

---

## Hardware Requirements

| Component | Description | Notes |
|---|---|---|
| 🔌 **Power Board** | Infineon 900V BDS EVB | Main power conversion stage |
| 🧠 **Control Board** | MCU Control Board (dsPIC) | Runs embedded firmware |
| ⚡ **Power Supply** | 12V DC | Powers MCU and gate drivers |
| 🔗 **Communication** | RS-485 Differential Connector | A+ / B- / GND |
| 💻 **Host PC** | Any Python 3.x capable machine | Runs main.py |

---

## Wiring / Connections

```
RS-485 Connector
┌──────────────────────────────────────┐
│  A+  →  Positive Data  (non-invert)  │
│  B-  →  Negative Data  (invert)      │
│  GND →  Common Ground                │
└──────────────────────────────────────┘
```

> ⚠️ **Critical:** A+ and B- **must not be swapped**.
> Reversed polarity will result in corrupted communication and no response from the MCU.

### Gate Drive Power
```
12V Supply ──► MCU Control Board ──► Gate Driver Power
                                 └──► dsPIC VDD (3.3V regulated)
```

---

## System Architecture

```
┌─────────────────────────────────┐
│      Python Host (main.py)      │
│   - Command interface           │
│   - Frequency / duty control    │
│   - Mode selection              │
│   - RDSon trigger               │
└────────────┬────────────────────┘
             │
             │  RS-485 Serial (A+/B-/GND)
             │  UART framed packets
             │
             ▼
┌─────────────────────────────────┐
│      MCU Control Board          │
│      dsPIC33 Series             │
│                                 │
│   - UART RX/TX (RS-485)         │
│   - PWM generation (118MHz clk) │
│   - Dead-time insertion         │
│   - Special event interrupt     │
│   - RDSon measurement FSM       │
│   - LED heartbeat               │
└────────────┬────────────────────┘
             │
             │  PWM1H / PWM1L
             │  PWM2H / PWM2L
             │  Control signals
             │
             ▼
┌─────────────────────────────────┐
│    Infineon 900V BDS EVB        │
│                                 │
│   - High side / Low side FETs   │
│   - Gate drivers                │
│   - Current / voltage sensing   │
└────────────┬────────────────────┘
             │
             ▼
     Power Conversion Output
```

---

## PWM Control Modes

### Mode 1 — Standard PWM
```
Both PWM1 and PWM2 operating in complementary mode
Shared master period (PTPER) and master duty cycle (MDC)
No dead-time insertion
Used for standard switching operation

PWM1H ─────┐     ┌─────┐     ┌────
           │     │     │     │
PWM1L ─────┘     └─────┘     └────
```

### Mode 2 — ZVS with Dead-Time
```
PWM1 → Complementary with programmable dead-time insertion
PWM2 → Both pins held HIGH via hardware override
       Used for synchronous rectifier / ZVS turn-on control

Dead-time prevents shoot-through during ZVS transitions
Configurable in nanoseconds via dt_ns parameter

PWM1H ──┐       ┌───────┐       ┌──
        │  DTR  │       │  DTR  │
PWM1L ──┘       └───────┘       └──
        ←  dt →         ←  dt →

PWM2H ─────────────────────────────  (constant HIGH)
PWM2L ─────────────────────────────  (constant HIGH)
```

### RDSon Measurement Cycle
```
Normal operation at user configured frequency
              │
              ▼  rdson_pending flag raised by host
Switch to 50kHz for exactly ONE PWM cycle  ←── Special Event ISR
              │
              ▼  cycle completes
Restore original frequency and duty cycle
              │
              ▼
rdson_cycle_done flag set → host reads measurement result
```

---

## Features

- 🔁 **Remote frequency control** — wide switching frequency range via Python
- 🎚️ **Remote duty cycle control** — 0 to 100% configurable from host
- 🔄 **Multiple PWM modes** — Standard complementary, ZVS dead-time, DyR
- ⏱️ **Programmable dead-time** — Nanosecond resolution, hardware enforced by dsPIC
- 📡 **RS-485 communication** — Differential, noise immune, suitable for high EMI lab environments
- 🛡️ **RDSon measurement** — Single cycle frequency switch for accurate RDSon capture
- 💡 **LED heartbeat** — Visual confirmation that firmware is alive and running
- 🔒 **Fault input disabled** — Clean operation during development and characterisation
- ⚙️ **Immediate register updates** — IUE mode ensures no deferred update issues on startup
- 🧠 **Interrupt driven** — Special Event and Timer1 interrupts handle all time critical tasks

---

## How It Works

### Firmware Side (dsPIC)
The dsPIC firmware initialises the auxiliary PLL to generate a **118MHz PWM clock**, giving
extremely fine frequency and dead-time resolution. The PWM peripheral operates in
**complementary mode** with a shared master timebase (PTPER) and master duty cycle (MDC),
ensuring both channels are always synchronised.

On receiving a command over RS-485 UART, the firmware parses the packet and updates the
relevant control variables. A pending flag system (`pwm_update_pending`, `freq_update_pending`,
`pwm_mode2_pending`) ensures register updates happen safely and are applied cleanly on the
next PWM cycle.

The **Special Event Interrupt** (`_PWMSpEventMatchInterrupt`) fires at a configurable point
in the PWM cycle and handles the RDSon measurement state machine — temporarily switching to
50kHz for one cycle to capture the RDSon signature, then restoring normal operation
automatically.

A **Timer1 interrupt** provides a low priority heartbeat tick, toggling the status LED
to confirm the firmware is running even when no commands are being received.

### Host Side (Python)
`main.py` opens a serial connection over RS-485 and presents a simple command line interface.
The user can set frequency, duty cycle and mode, or trigger special measurement sequences.
All commands are framed and transmitted to the MCU, with responses parsed and displayed
in the terminal.

---

## Project Structure

```
BDS_CONTROL.X/
│
├── main.c              # Main application loop, command parser, state machine
├── PWM.c               # PWM initialisation, update functions, mode control
├── PWM.h               # PWM function prototypes and defines
├── UART.c              # RS-485 UART transmit and receive
├── UART.h              # UART function prototypes
├── Clock.c             # PLL and auxiliary clock initialisation
├── IO.c                # GPIO and pin configuration
│
├── main.py             # Python host control interface
│
└── README.md           # This file
```

---

## Known Limitations

- ⚠️ **Unidirectional devices only** — bidirectional operation not currently supported
- ⚠️ **No closed loop control** — frequency and duty are open loop, set manually by user
- ⚠️ **RS-485 polarity sensitive** — swapped A+/B- will result in no communication
- ⚠️ **Dead-time clamped at 500ns** — hardware limit enforced in firmware (59 counts max)
- ⚠️ **RDSon cycle is blocking** — normal operation pauses for one 50kHz cycle during measurement

---

## Notes

- Always apply **12V supply before** launching Python interface
- Confirm **LED heartbeat is blinking** before sending any commands
- RS-485 cable should be **twisted pair** for best noise immunity in lab environments
- Keep RS-485 cable away from **gate drive lines** to avoid coupling noise into comms
- When switching modes, allow **at least one PWM cycle** before sending the next command
- All frequency values are in **Hz** (e.g. 100000 = 100kHz)
- All duty values are in **percent** (e.g. 50 = 50%)

---

## License

Internal use only — Infineon New product development
