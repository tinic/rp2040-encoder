# RP2040 Quadrature Encoder Firmware

C++23 firmware for the RP2040-based USB quadrature encoder interface that
mirrors a 5 V TTL DRO into LinuxCNC. See [`../hardware/README.md`](../hardware/README.md)
for the wiring and the [`../README.md`](../README.md) for the project
overview.

## Architecture highlights

- PIO state machines decode A/B quadrature on GPIO 0–7 with a 16-entry
  jump-table walker (4 SMs, one per axis).
- A DMA reload pair per encoder drains the PIO RX FIFO into a `volatile
  int32_t positions[N]` slot with zero CPU involvement; CPU reads
  positions[] directly via const member functions.
- A GPIO interrupt per axis on GP26–GP29 latches the count at the scale's
  reference (R / index) pulse and counts the pulses; unwired inputs are
  held inactive by an internal pull. After an edge the input is ignored
  until the axis has moved 32 counts away, so that the slow flank of a
  pulse counts once.
- USB vendor-class device (VID `0x2E8A`, PID `0xC0DE`) speaks the
  request/response protocol below; framing is one command per transfer,
  responses are 56 bytes (one short packet on the 64-byte endpoint), every
  write is followed by `tud_vendor_n_write_flush`.
- Watchdog reset at 1000 ms keeps a hung `tud_task` from wedging the
  device; reboot triggers USB re-enumeration.

## Building

### Requirements

- CMake 3.14 or later
- `gcc-arm-none-eabi` cross-compiler
- `libusb-1.0` development headers (`libusb-1.0-0-dev` on Debian/Ubuntu)
  — needed at configure/build time to compile the USB-enabled picotool
  used by `cmake --install`. Skip if you'll never use `cmake --install`.
- The Pico SDK submodule (auto-initialised by CMake on first configure)

### Configure + build

```bash
cd rp2040-firmware
cmake -B build
cmake --build build -j
```

The build produces `build/rp2040-hal-encoder.uf2` (the flashable
firmware) and `build/rp2040-hal-encoder.elf` (for debugging with
`picotool info` or gdb).

It also builds a separate `build/picotool-usb-build/picotool` (the
USB-enabled picotool the install rule needs — see below). The pico-SDK
bundles its own picotool but builds it with `PICOTOOL_NO_LIBUSB=1` (UF2
manipulation only), so we fetch and build a second copy with USB
support enabled.

## Flashing

### `cmake --install` (recommended)

```bash
cmake --install build
```

Runs the USB-enabled picotool with `load -f -x`: force-reboots the
device into BOOTSEL if it's currently running our firmware, loads the
new UF2, then re-launches so the device re-enumerates on the USB bus.

No need to hold BOOTSEL. Just have the device plugged in.

### BOOTSEL + mass storage

Old-school path, no picotool needed:

1. Hold the BOOTSEL button while plugging the RP2040 into USB.
2. The device appears as `RPI-RP2` mass storage.
3. Copy `build/rp2040-hal-encoder.uf2` onto it.
4. Device reboots into the new firmware.

The CMakeLists also provides `flash` / `flash-quick` make targets that
do this copy automatically; see the `FLASH_DEVICE_PATH` cache variable.

## USB Protocol

Vendor-class endpoints (VID `0x2E8A`, PID `0xC0DE`):

- `EP_OUT 0x01` — host writes one command per transfer.
- `EP_IN 0x81` — device writes one 56-byte response per transfer.

### Commands (host → device)

| Opcode | Name                | Payload                                | Response          |
|--------|---------------------|----------------------------------------|-------------------|
| `0x01` | GET_POSITION        | none                                   | 56-byte position  |
| `0x02` | SET_TEST_MODE       | 1 byte: 0=off, 1=sine, 2=circular, 3=ramp, 4=random | none |
| `0x03` | SET_SCALE           | 1 byte idx + 8-byte double; auto-persists after 1 s debounce | none |
| `0x04` | GET_SCALE           | none                                   | 56-byte scale     |
| `0x05` | RESET_POSITION      | 1 byte idx                             | none              |
| `0x06` | GET_VERSION         | none                                   | 56-byte version   |
| `0x07` | BOOTSEL             | none; reboots into BOOTSEL (2e8a:0003) | (device drops off bus) |
| `0x08` | RESET_CONFIG        | none; erases the persisted scales blob; takes effect on next boot | none |
| `0x09` | SET_REF_CONFIG      | 1 byte enable mask + 1 byte invert mask, bit N for encoder N; auto-persists after 1 s debounce | none |
| `0x0A` | GET_REF_POSITION    | none                                   | 56-byte reference positions |
| `0x0B` | ARM_INDEX           | 1 byte idx + 1 byte (1 arm, 0 disarm): zero that count at its next reference pulse | none |

### Responses (device → host)

Every response is exactly 56 bytes. `GET_POSITION` and `GET_SCALE`:

| Offset | Size | Contents                                                                |
|--------|------|-------------------------------------------------------------------------|
| 0      | 4    | Sentinel: `0x3F8A7C91` for position, `0x7B2D4E8F` for scale (little-endian) |
| 4      | 32   | 4 little-endian doubles                                                 |
| 36     | 20   | `GET_POSITION`: reference status and pulse counts; `GET_SCALE`: zero    |

Bytes 36 to 55 of the `GET_POSITION` response:

| Offset | Size | Contents                                                                |
|--------|------|-------------------------------------------------------------------------|
| 36     | 1    | Reference enable mask, bit N for encoder N                              |
| 37     | 1    | Reference invert mask: 1 is active low, counted on the falling edge     |
| 38     | 1    | Reference inputs active now (after inversion)                           |
| 39     | 1    | Index armed mask: bit N is cleared when the armed pulse has zeroed the count |
| 40     | 16   | 4 little-endian `uint32` reference pulse counts since boot              |

`GET_REF_POSITION` response:

| Offset | Size | Contents                                                                |
|--------|------|-------------------------------------------------------------------------|
| 0      | 4    | Sentinel `0x9C4E2A71` (little-endian)                                   |
| 4      | 32   | 4 little-endian doubles: the scaled position latched at the latest reference pulse of each encoder |
| 36     | 16   | 4 little-endian `uint32` reference pulse counts, as in `GET_POSITION`   |
| 52     | 4    | Zero                                                                    |

The HAL component reads the counts with every `GET_POSITION` and asks
for `GET_REF_POSITION` only when a count has changed. The latched position
is relative to the count's zero at the moment of the pulse: a later
`RESET_POSITION`, or the zeroing of an armed index, does not move it.

`GET_VERSION` response:

| Offset | Size | Contents                                                                |
|--------|------|-------------------------------------------------------------------------|
| 0      | 4    | Sentinel `0x5A1B9C3D` (little-endian)                                   |
| 4      | 52   | Zero-padded ASCII `GIT_VERSION` (`v1.0.0`, `v1.0.0-3-gabc-dirty`, ...)  |

Responses are shorter than the endpoint's 64-byte packet on purpose. A
response that fills a packet exactly is followed by a zero-length packet
that ends the transfer; a host reading 64 bytes leaves that empty packet
queued, its next read returns nothing, and every answer arrives one
request late. That is what firmware up to v3.0.0 did with its 64-byte
responses. A 56-byte response is one short packet with nothing after it.

Framing is one transfer per command and one short packet per response.
The sentinel says which request a response answers; a host should check
it and skip frames with another one.

## Persistent configuration

Scale factors (per-encoder counts-per-unit, set via `SET_SCALE`) and the
reference enable and invert masks (set via `SET_REF_CONFIG`; default: all
four enabled, active high) are written to the last 4 KiB sector of flash
automatically, 1 s after the last `SET_SCALE` or `SET_REF_CONFIG` arrives. The 1 s debounce coalesces bursts (e.g. the
HAL component sending all four axes on connect) into a single flash
write. Duplicate writes (same value) are deduplicated and do not trigger
a save, so steady-state operation does no flash wear.

On boot, `Position::init()` reads the blob and applies it before the
LinuxCNC HAL has had a chance to (re-)send scale factors. The on-disk
format carries a CRC32 and a schema version: corruption or an unknown
schema causes the load to be skipped and the compile-time defaults in
`main.cpp` to apply. Schema 2 added the reference masks; a schema 1 blob
(scales only) is read with the default masks.

`RESET_CONFIG` (`0x08`) wipes the sector. After the next reboot, the
device behaves as if it had never been configured.

Flash wear: ~10,000 erase cycles per sector. With dedup + debounce, a
typical LinuxCNC user changing their CNC config once a day would take
~27 years to exhaust it.

## Test Mode

Four built-in patterns for development without scales attached. Enable
in firmware via `Position::enable_test_mode(true)` + `set_test_pattern(N)`,
or at runtime via the `SET_TEST_MODE` USB command:

| Pattern | Mode               | Notes                                            |
|---------|--------------------|--------------------------------------------------|
| 0       | SINE_WAVE          | Per-axis sinusoidal motion                       |
| 1       | CIRCULAR           | XY circle, Z slow oscillation, A constant spin   |
| 2       | LINEAR_RAMP        | Constant per-axis velocity                       |
| 3       | RANDOM_WALK        | Small random steps simulating measurement noise  |

## Device Identification

USB serial-number string format:

```
4ENC-<git-describe>
```

Where `<git-describe>` is the output of `git describe --tags --always
--dirty --match "v*"`. For tagged releases that's just `v1.0.0`; for
dev builds it's e.g. `v1.0.0-3-gf7c079c` or `v1.0.0-3-gf7c079c-dirty`
when there are uncommitted changes.

## Testing

Use the Python script in the repo root:

```bash
pip install pyusb
python3 ../test_usb_device.py    # sudo on Linux unless udev rules are set
```

## Troubleshooting

### `cmake --install` fails with "USB-enabled picotool was not built"

Run `cmake --build build` first; the install rule depends on a built
picotool which the firmware target auto-triggers.

### `picotool failed (rc=249)`

`No accessible RP-series devices in BOOTSEL mode were found.` Usually
means the device isn't plugged in, or the host lacks permission. On
Linux, install the picotool udev rules (Ubuntu's `picotool` apt package
ships them in `/lib/udev/rules.d/`), or copy the file from
[upstream picotool](https://github.com/raspberrypi/picotool/blob/master/udev/99-picotool.rules).

### `libusb.h: No such file or directory` during picotool build

Install `libusb-1.0-0-dev` (Debian/Ubuntu) or your distro's equivalent.

### Position drift after scale power cycle

See the ISO7760F default-LOW caveat in [`../hardware/README.md`](../hardware/README.md).

## LinuxCNC HAL

See [`../linuxcnc-hal/README.md`](../linuxcnc-hal/README.md) for the
LinuxCNC userspace component that consumes this firmware.
