# RP2040 LinuxCNC HAL Quadrature Encoder Interface

A USB quadrature encoder interface that connects DRO scales to LinuxCNC using an RP2040 microcontroller.

## Overview

This project allows you to connect existing DRO (Digital Read Out) scales with TTL level A/B quadrature signals to LinuxCNC through a simple USB connection. It splices into the existing DB9 connector/cables so your existing DRO continues to function. Common brands which work include [TOAUTO](https://www.toautotool.com/products/2-3-axis-dro-kit-standard-scales) and other similar 5V TTL quadrature encoder scales. It uses readily available components:

**Waveshare RP2040-Zero:**

[<img src="./hardware/rp2040-zero.jpg" width="200px"/>](./hardware/rp2040-zero.jpg)

**Texas Instruments ISO7760FDBQR digital isolator** (6-channel forward, SSOP-16, default-LOW *F* variant) provides 5 kVrms galvanic isolation between the machine-side electrical environment and the USB host. See [`hardware/README.md`](hardware/README.md) for the full pinout and a known startup-glitch caveat tied to the F suffix.

## Finished board

[<img src="./hardware/IMG_8394.jpg" width="300px"/>](./hardware/IMG_8394.jpg)

Spliced into the existing DRO, there are likely nicer looking solutions than this:

[<img src="./hardware/IMG_8388.jpg" width="300px"/>](./hardware/IMG_8388.jpg)

## Features

- 3 DRO scales (X, Y, Z) with TTL A/B quadrature signals; firmware supports a 4th channel if hardware is extended
- High-speed PIO-based edge counting
- DMA reload pair per channel drains the PIO RX FIFO into memory with zero CPU touch
- 32-bit position counters
- USB interface with LinuxCNC HAL component
- Configurable scale factors (compile-time defaults overridable at runtime over USB / HAL)
- Test mode for development and debugging
- Watchdog reboot on firmware hang
- Galvanic isolation between machine and host PC

## Quick Start

See the individual directories for detailed instructions:
- [`rp2040-firmware/`](rp2040-firmware/) - RP2040 firmware
- [`linuxcnc-hal/`](linuxcnc-hal/) - LinuxCNC HAL component
- [`hardware/`](hardware/) - Hardware setup and wiring
