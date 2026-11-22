# LinuxCNC HAL Component for RP2040 Quadrature Encoder Interface

This directory contains the LinuxCNC HAL component for interfacing with the RP2040 USB quadrature encoder device.

## Requirements

- LinuxCNC 2.7 or later
- libusb-1.0 development files (`sudo apt-get install libusb-1.0-0-dev`)
- halcompile (included with LinuxCNC)

## Installation

1. Compile and install the component:
   ```bash
   sudo halcompile --install rp2040_encoder.comp
   ```

2. Install the udev rule shipped in this directory so neither the HAL
   component nor `picotool` needs root:
   ```bash
   sudo cp 99-rp2040-encoder.rules /etc/udev/rules.d/
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   sudo usermod -a -G plugdev "$USER"   # log out / back in to apply
   ```
   The rule grants the `plugdev` group access to the device in normal
   operating mode (`2e8a:c0de`) and BOOTSEL mode (`2e8a:0003`).

## Usage

The component is loaded as `rp2040_encoder`; its pins are named `rp2040-encoder.0.*`
(halcompile turns the underscore of the component name into a dash).

In your HAL configuration file:

```hal
# Load the component
loadusr -W rp2040_encoder

# Units per count of each scale, and the direction of each axis
setp rp2040-encoder.0.scale-0 0.005
setp rp2040-encoder.0.scale-1 0.005
setp rp2040-encoder.0.scale-2 0.005
setp rp2040-encoder.0.sign-0 -1

# Connect encoder position outputs to your DRO display
net x-pos rp2040-encoder.0.position-0 => pyvcp.x-dro
net y-pos rp2040-encoder.0.position-1 => pyvcp.y-dro
net z-pos rp2040-encoder.0.position-2 => pyvcp.z-dro
net a-pos rp2040-encoder.0.position-3 => pyvcp.a-dro

# Monitor connection status
net encoder-connected rp2040-encoder.0.connected => pyvcp.encoder-connected-led
```

## HAL Pins

`N` is the encoder: 0 is X, 1 is Y, 2 is Z, 3 is A.

### Position

| Pin | Type | Default | Meaning |
|-----|------|---------|---------|
| `position-N` | float out | | Position of the scale: counts times the scale factor, times `sign-N` |
| `connected` | bit out | | True while the USB device is open and answering. The position pins keep their last value while it is false |
| `scale-N` | float in | 0 | Units per count. Sent to the device whenever it changes, and kept there in flash. An axis whose pin is left at 0 gets the scale 0 and reads 0: set the pin of every axis you use |
| `scale-fb-N` | float out | | The scale factor the device reported when it connected |
| `sign-N` | s32 in | 1 | A negative value inverts `position-N` and `ref-position-N`; zero and positive values leave them as they are. Builds before v3.0.0 inverted every axis; set −1 to keep the directions of such a configuration |
| `reset-N` | u32 in | 0 | A change of the value sets the count of that scale to zero |
| `test-mode` | s32 in | 0 | 0 is off; 1 sine, 2 circular, 3 linear ramp and 4 random walk replace the scale positions with a pattern |

### Reference (R / index) pulse

Firmware and component v3.0.0 and later read the reference pulse of each scale on GP26
to GP29; see [`../hardware/README.md`](../hardware/README.md) for the wiring.

| Pin | Type | Default | Meaning |
|-----|------|---------|---------|
| `ref-enable-N` | bit in | 1 | Recognise reference pulses on this scale. Kept in the device's flash |
| `ref-invert-N` | bit in | 0 | 0: the pulse is active high and counted on its rising edge. 1: active low, counted on its falling edge. Kept in the device's flash |
| `ref-count-N` | u32 out | | Reference pulses seen since the device was powered or reset: one per crossing of a mark with firmware after v3.0.0, possibly many with v3.0.0 (see below). It starts at 0 again after a reboot of the device, not after a restart of LinuxCNC |
| `ref-position-N` | float out | | Position of the scale at the latest reference pulse, latched in the device at the edge, in the units and sign of `position-N`. It follows `ref-count-N` by one USB round trip |
| `ref-state-N` | bit out | | The reference input is active now. A pulse can be shorter than the time between two readings: use `ref-count-N` to see that one happened |
| `index-enable-N` | bit io | 0 | LinuxCNC's index handshake: set it and the device zeroes the count of that scale at the next reference pulse, then the component clears the pin |

Things to know when using them:

- A pulse is counted on an edge. An axis that stands on its reference mark when the
  device starts has `ref-state-N` true and `ref-count-N` 0 until it leaves the mark and
  crosses it again.
- Firmware after v3.0.0 counts one edge per crossing: after an edge the input does not
  count again until the axis has moved 32 counts away from it (0.16 mm at 0.005 mm per
  count) and the input has been inactive, without an edge, for 2 ms. `ref-position-N`
  holds that first edge. Two crossings closer together than that, in distance or in
  time, count once. Arming `index-enable-N` lets the input count again at once.
- Firmware v3.0.0 does not debounce the input: on one scale a slow pass over the mark
  counted about 250 edges within a few counts, where passes by hand had counted one each,
  and `ref-position-N` held the last of them. With that firmware treat a change of
  `ref-count-N` as "the mark was crossed", not as a number of crossings.
- `ref-position-N` is in the count the scale had when the pulse arrived. Zeroing the
  count afterwards, with `reset-N` or through `index-enable-N`, does not move the latched
  value with it: after that, wait for the next pulse before using `ref-position-N`.
- The latched position of one mark differs by a few counts between the two directions
  of travel (0.02 to 0.03 mm at 0.005 mm per count on the scales of one mill), because
  the pulse has a width. Approach a mark from the same side whenever the position
  matters.
- The component runs in userspace. With `index-enable-N` wired to
  `joint.N.index-enable`, the zeroed position and the cleared pin reach HAL in two
  separate writes, which a servo thread can see apart. For homing, a realtime component
  that does the handshake from `ref-count-N` and leaves the count alone
  (`HOME_INDEX_NO_ENCODER_RESET = YES`) avoids that; `refmark` in
  [tinic/linuxcnc](https://github.com/tinic/linuxcnc) does it for scales with a single
  reference mark.

```hal
# Watch the reference pulses
halcmd show pin rp2040-encoder.0.ref
```

## Firmware and component versions

| Component | Firmware | Works |
|-----------|----------|-------|
| after v3.0.0 | after v3.0.0 (56-byte responses) | Yes |
| after v3.0.0 | up to v3.0.0 (64-byte responses) | Yes: the component skips the empty packet that follows each response |
| v3.0.0 | after v3.0.0 | Not tested. From the source: positions and reference data are read, the firmware version is not printed |
| v3.0.0 | v3.0.0 | `ref-position-N` can stay at 0 although `ref-count-N` counts (seen on one machine): update the component |

The component prints the firmware version when it connects
(`rp2040_encoder: Firmware version: ...`).

## Troubleshooting

1. Check USB connection:
   ```bash
   lsusb | grep 2e8a:c0de
   ```
   
   The device serial number shows hardware config and firmware info:
   `{N}ENC-{git_hash}-{build_date}` (e.g., "4ENC-2d49ae7-2025-06-17")

2. Check HAL component is loaded:
   ```bash
   halcmd show comp rp2040_encoder
   ```

3. Monitor HAL pins:
   ```bash
   halcmd show pin rp2040_encoder
   ```

4. Check for USB permission issues:
   ```bash
   dmesg | tail
   ```