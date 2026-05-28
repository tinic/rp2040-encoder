# Hardware Setup - RP2040 Quadrature Encoder Interface

This document describes the hardware setup for the RP2040-based USB
quadrature encoder interface for LinuxCNC DRO (Digital Readout) systems.

## Overview

The system reads 3 quadrature encoders (X, Y, Z) and reports position
over USB to LinuxCNC. PIO state machines on the RP2040 do the edge
counting; a DMA reload pair per channel drains the PIO RX FIFOs into
memory with zero CPU involvement. Encoder signals cross a 5 kVrms
galvanic isolation barrier (ISO7760FDBQR) before reaching the RP2040
GPIOs, protecting the USB host from machine-side electrical events.

The firmware also supports a 4th encoder channel on GPIO 6/7 (rotary
A-axis), but that channel is **not** wired through on the isolation
board described here — the ISO7760FDBQR has exactly 6 forward channels,
fully consumed by 3 axes × 2 quadrature lines.

## Required Components

| Component | Notes |
|-----------|-------|
| **Waveshare RP2040 Zero** | Compact RP2040 board with built-in WS2812 RGB status LED |
| **ISO7760FDBQR** | TI 6-channel forward digital isolator, SSOP-16, default-LOW (the *F* suffix) |
| 10-pin 2.54 mm PCB terminal block | Encoder cable terminations |
| Glass scales / TTL quadrature encoders | 5 V A/B signals |
| TOAUTO DRO (optional) | Original DRO; can run in parallel via DB9 taps |
| Jienk DB9 M/F breakout boards | One per axis, used to tap scale signals without cutting cables |

### Connection Strategy

The existing glass scale ↔ TOAUTO DRO link is intercepted with DB9
breakout boards. A/B signals branch off to the terminal block on the
RP2040 enclosure; the scales stay wired to the DRO so the original
display continues to work. The ISO7760FDBQR sits in the RP2040 enclosure
and isolates the machine's electrical environment from the USB host PC.

## Pin Assignments

### Quadrature Encoder Inputs

| Encoder | Axis | RP2040 GPIO | ISO7760F output | ISO7760F input | Terminal |
|---------|------|-------------|-----------------|----------------|----------|
| 0       | X    | GPIO 0 (A)  | OUTA (pin 15)   | INA (pin 2)    | A        |
| 0       | X    | GPIO 1 (B)  | OUTB (pin 14)   | INB (pin 3)    | B        |
| 1       | Y    | GPIO 2 (A)  | OUTC (pin 13)   | INC (pin 4)    | A        |
| 1       | Y    | GPIO 3 (B)  | OUTD (pin 12)   | IND (pin 5)    | B        |
| 2       | Z    | GPIO 4 (A)  | OUTE (pin 11)   | INE (pin 6)    | A        |
| 2       | Z    | GPIO 5 (B)  | OUTF (pin 10)   | INF (pin 7)    | B        |

Power and ground:

| ISO7760F pin | Net          | Notes                          |
|--------------|--------------|--------------------------------|
| VCC1 (pin 1) | +5 V         | Encoder side, from DRO supply  |
| GND1 (pin 8) | Machine GND  | Encoder side                   |
| VCC2 (pin 16)| +3.3 V       | RP2040 side, from `3V3` pin    |
| GND2 (pin 9) | RP2040 GND   | RP2040 side                    |

Decoupling: 0.1 µF from VCC1→GND1 and VCC2→GND2, placed close to the
package.

## ISO7760FDBQR Setup

The ISO7760FDBQR is a **unidirectional** 6-channel digital isolator: all
6 channels go INA-F → OUTA-F. The encoder side must be wired to the
**input pins** (pins 2–7), and the RP2040 side to the **output pins**
(pins 10–15). Reversing this will not work — there is no auto-direction
detection like the older parts.

### Default-LOW behavior (the *F* suffix)

The `F` in `ISO7760FDBQR` is load-bearing:

- **With F (this part)**: outputs go **LOW** when the input pin is
  floating, when VCC1 is unpowered, or when the input cable is
  disconnected.
- **Without F**: outputs would go **HIGH** in the same conditions.

This matters at startup and during scale disconnect events. If the
scales are unpowered or their cables aren't yet biased while the RP2040
side is alive, the RP2040 sees `A=0, B=0` on every channel. When the
scales come up at their true logic levels (often `A=0, B=1` or `A=1,
B=0`), the PIO observes a `00 → 01` or `00 → 10` transition and records
a **spurious ±1 count** before any real motion has occurred. The same
happens in reverse on power-down.

Mitigation: zero the affected axes from LinuxCNC after every full
power-up cycle, or sequence scale power and USB connect so that the
scales are biased before the RP2040 enumerates. Verify you have the
**F variant** physically — the standard ISO7760 (no F) inverts this
behavior and produces opposite-polarity startup glitches.

### Why no internal pulls

The ISO7760F outputs are CMOS push-pull (~20 Ω driver, ±15 mA absolute
max), strong enough to drive RP2040 GPIO inputs directly with no
external resistors. The firmware leaves the RP2040 internal pulls
disabled (`gpio_set_pulls(pin, false, false)` in
`quadrature_encoder.pio`).

If your encoders are **open-collector / open-drain** rather than
push-pull TTL, add 1–10 kΩ pull-ups to +5 V on the **input side** of the
ISO7760F (between the terminal block and pins 2–7). Most modern TTL
scales are push-pull and don't need this.

## TOAUTO DRO Integration

### DB9 Pinout (per axis)

| Pin | Signal | Description           |
|-----|--------|-----------------------|
| 1   | +5 V   | Power supply          |
| 2   | 0 V    | Ground                |
| 3   | A      | Quadrature A (TTL)    |
| 4   | B      | Quadrature B (TTL)    |
| 5–9 | NC     | Not connected         |

### Per-axis wiring

For each of X, Y, Z:

1. Use a Jienk DB9 male/female breakout board.
2. Glass scale → Female DB9.
3. TOAUTO DRO → Male DB9.
4. Tap pins 3 (A) and 4 (B) at the breakout to the RP2040 enclosure
   terminal block.
5. Tie all DB9 pin-2 grounds to the ISO7760F GND1 net.
6. Tie all DB9 pin-1 +5 V together as the VCC1 net (also powers the
   isolator's encoder side).

## Encoder Specifications

| Property          | Value                                          |
|-------------------|------------------------------------------------|
| Voltage levels    | 5 V TTL (isolated to 3.3 V across the barrier) |
| Signal type       | Single-ended A/B quadrature                    |
| Max edge rate     | ≪ 100 Mbps isolator limit; PIO sample rate ≫ any realistic encoder |
| Channels per axis | 2 (A and B); index/Z not used                  |

## Assembly Steps

1. Flash the firmware (see `rp2040-firmware/README.md`).
2. Hand-wire the ISO7760FDBQR to the RP2040 Zero per the pin table
   above. Decoupling caps as close to VCC1/VCC2 as practical.
3. Wire the terminal block to the ISO7760F input side (pins 2–7) and to
   the VCC1 / GND1 nets.
4. Wire the DB9 breakouts and connect glass scale ↔ DRO through them,
   tapping A/B/+5/GND off to the terminal block.
5. Power the DRO, then connect USB to the host PC.
6. Confirm enumeration as VID:PID `2e8a:c0de`.
7. Run `test_usb_device.py` to verify counts respond to scale movement.

### Build photos

RP2040-Zero with the ISO7760F hand-wired alongside, feeding the terminal
block:

![Internal wiring](IMG_8394.jpg)

Assembled enclosure with three DB9 axis breakouts:

![Assembled device](IMG_8391.jpg)

Installed in the machine, tapping the existing scale ↔ DRO cables:

![In-machine installation](IMG_8388.jpg)

## Troubleshooting

### No counts on any axis
- Confirm VCC1 (5 V) and VCC2 (3.3 V) are both present, with GND1/GND2
  tied to their respective ground references.
- Verify the ISO7760F is the **F suffix** part (or note the inverted
  default behavior described above).
- Check the DB9 → terminal block A/B wiring matches the table; ISO7760F
  input pins are 2–7, output pins are 15–10 (mirrored).

### Counts drift after scale power cycle
- Expected — see the default-LOW startup-glitch note above. Zero the
  affected axis in LinuxCNC after power events.

### Counts in wrong direction
- Swap A and B at the terminal block, **or** set a negative scale factor
  from the host (USB `SET_SCALE` command / HAL `scale-N` pin).

### Erratic counts with no motion
- Most likely an open-collector encoder without a pull-up on the input
  side. Add 1–10 kΩ pull-ups to +5 V on the affected channels.
- Check the scale cable shield is grounded only at one end.

### DRO continues to work but USB doesn't
- Verify the ISO7760F output side is wired to the **RP2040** (pins
  15–10 → GPIOs 0–5), not the encoder side. The chip is unidirectional;
  swapping breaks it.

### USB enumerates but reads fail
- Check `dmesg` for libusb permission errors; a udev rule for VID 2e8a
  PID c0de may be required.

## Configuration

Scale factors are configured in `rp2040-firmware/main.cpp` and overridable
at runtime via the LinuxCNC HAL `scale-N` pins:

```cpp
pos.set_scale(0, 0.001);  // X: 0.001 mm/count (1000 counts/mm)
pos.set_scale(1, 0.001);  // Y: 0.001 mm/count
pos.set_scale(2, 0.001);  // Z: 0.001 mm/count
pos.set_scale(3, 0.1);    // A: 0.1 deg/count — unused on this board
```

Common defaults:
- Glass scales: 0.001 mm/count (1 µm resolution)
- Rotary encoders: 360 / (PPR × 4) deg/count

## Safety

- Power off everything before changing wiring.
- The ISO7760F provides 5 kVrms isolation; do not bypass it by tying
  GND1 and GND2 together.
- Use proper enclosures and ESD handling.
