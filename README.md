# RP2040 LinuxCNC HAL Quadrature Encoder Interface

**Bridge an existing 5 V TTL DRO (Digital Read Out) — TOAUTO, Sino, Ditron, generic glass-scale kits — into LinuxCNC over USB without removing the DRO.** Think of it as *TouchDRO's spirit, but the host is LinuxCNC instead of an Android tablet*: splice into the existing DB9 cabling, keep the original DRO display working, and mirror the X/Y/Z position counts into LinuxCNC as a HAL component.

## Overview

This project connects existing DRO scales with TTL-level A/B quadrature signals to LinuxCNC through a simple USB connection. It splices into the existing DB9 connectors/cables so the original DRO continues to function. Common brands which work include [TOAUTO](https://www.toautotool.com/products/2-3-axis-dro-kit-standard-scales), Sino, Ditron, and other 5 V TTL quadrature glass-scale and magnetic-scale kits.

The hardware is intentionally minimal:

**Waveshare RP2040-Zero** — compact RP2040 board, USB-C, built-in WS2812 status LED.

[<img src="./hardware/rp2040-zero.jpg" alt="Waveshare RP2040-Zero board" width="200px"/>](./hardware/rp2040-zero.jpg)

**Texas Instruments ISO7760FDBQR digital isolator** — 6-channel forward, SSOP-16, default-LOW *F* variant. Provides 5 kVrms galvanic isolation between the machine-side electrical environment and the USB host PC, important on machines with VFDs, servo drives, or noisy ground references. See [`hardware/README.md`](hardware/README.md) for the full pinout and a startup-glitch caveat tied to the F suffix.

## Finished board

[<img src="./hardware/IMG_8394.jpg" alt="RP2040-Zero with ISO7760F digital isolator on a hand-wired board" width="300px"/>](./hardware/IMG_8394.jpg)

Spliced into the existing DRO via DB9 breakouts — there are likely nicer-looking solutions than this:

[<img src="./hardware/IMG_8388.jpg" alt="USB DRO bridge installed on a CNC machine, tapping into glass scale cables" width="300px"/>](./hardware/IMG_8388.jpg)

## Features

- 3 DRO scales (X, Y, Z) with TTL A/B quadrature signals; firmware supports a 4th channel if hardware is extended
- PIO-based edge counting on the RP2040 — no software polling loop, no missed transitions
- DMA reload pair per channel drains the PIO RX FIFO into memory with **zero CPU involvement**, so position counts cannot drop regardless of scale velocity
- 32-bit signed position counters per axis
- USB vendor-class device + LinuxCNC HAL userspace component
- Configurable per-axis scale factors (compile-time defaults overridable at runtime over USB or via HAL pins)
- Built-in test mode (sine / circular / linear / random) for development and debugging without scales attached
- Hardware watchdog: automatic reboot + USB re-enumeration on firmware hang
- 5 kVrms galvanic isolation between machine and host PC

## Quick Start

See the individual directories for detailed instructions:
- [`rp2040-firmware/`](rp2040-firmware/) — RP2040 firmware (Pico SDK 2.2.0, C++23)
- [`linuxcnc-hal/`](linuxcnc-hal/) — LinuxCNC HAL component (`halcompile --install`)
- [`hardware/`](hardware/) — Hardware setup, isolator wiring, and DB9 splice diagrams

## How this compares to other projects

The DIY DRO and LinuxCNC encoder-interface space has a few well-known projects; this one occupies a specific niche.

| Project | Host | Connection | Goal |
|---------|------|------------|------|
| **This project** | LinuxCNC | USB | Mirror an existing DRO into LinuxCNC as position feedback, preserve the original display |
| [TouchDRO (Yuriy Krushelnytskiy)](https://www.touchdro.com/resources/adapters/diy/) | Android / Fire tablet | Bluetooth | Replace a physical DRO display with a tablet UI |
| [atrex66/stepper-ninja](https://github.com/atrex66/stepper-ninja) | LinuxCNC | UDP / Ethernet | RP2040 as a deterministic motion controller (step generation + encoder feedback) |
| [Remora](https://github.com/scottalford75/Remora) | LinuxCNC on RPi | SPI | STM32/LPC176x as a realtime co-processor for motion |
| [Mesa hostmot2 cards](https://linuxcnc.org/docs/html/man/man9/hostmot2.9.html) | LinuxCNC | PCI / Ethernet | FPGA-based deterministic motion + encoder, commercial gold standard |
| [adamgreen/QuadratureDecoder](https://github.com/adamgreen/QuadratureDecoder) | (library only) | — | PIO+DMA RP2040 quadrature library; no USB/CNC integration |

**Why USB and not Ethernet?** USB is non-deterministic by design and is the wrong tool for LinuxCNC's realtime motion loop. It's the right tool for **position display / DRO mirroring**, where missing a single millisecond update doesn't matter and only the integral count needs to be correct — which is exactly what this project does. If you need closed-loop motion feedback for servoing, look at Mesa, Remora, or stepper-ninja.

**Why an isolator rather than a level shifter?** Glass-scale cables run through the machine alongside power and motor wiring. Galvanic isolation between the machine ground and the host PC's USB ground avoids ground loops, VFD/servo noise injection, and reduces the blast radius of a wiring fault. The earlier revision of this project used a TXS0108E bidirectional level shifter; the current design uses an ISO7760FDBQR digital isolator.

## Keywords

LinuxCNC, DRO, digital readout, glass scale, magnetic scale, quadrature encoder, TTL, 5V TTL, RP2040, Raspberry Pi Pico, ISO7760, digital isolator, galvanic isolation, USB, HAL component, TOAUTO, Sino, Ditron, CNC, milling machine, lathe, retrofit, splice, position feedback, TouchDRO alternative.
