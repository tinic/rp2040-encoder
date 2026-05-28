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

In your HAL configuration file:

```hal
# Load the component
loadusr -W rp2040_encoder

# Connect encoder position outputs to your DRO display
net x-pos rp2040_encoder.0.position-0 => pyvcp.x-dro
net y-pos rp2040_encoder.0.position-1 => pyvcp.y-dro
net z-pos rp2040_encoder.0.position-2 => pyvcp.z-dro
net a-pos rp2040_encoder.0.position-3 => pyvcp.a-dro

# Monitor connection status
net encoder-connected rp2040_encoder.0.connected => pyvcp.encoder-connected-led
```

## HAL Pins

- `rp2040_encoder.0.position-0` (float, out) - Encoder 0 position value (X axis)
- `rp2040_encoder.0.position-1` (float, out) - Encoder 1 position value (Y axis)
- `rp2040_encoder.0.position-2` (float, out) - Encoder 2 position value (Z axis)
- `rp2040_encoder.0.position-3` (float, out) - Encoder 3 position value (A axis)
- `rp2040_encoder.0.connected` (bit, out) - True when USB device is connected

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