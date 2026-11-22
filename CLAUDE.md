# RP2040 LinuxCNC HAL Quadrature Encoder Interface

This project implements a USB quadrature encoder interface on RP2040-based boards that connects DRO scales with TTL level A/B quadrature signals to LinuxCNC through a HAL (Hardware Abstraction Layer) component. Compatible with common brands like [TOAUTO](https://www.toautotool.com/products/2-3-axis-dro-kit-standard-scales) and other 5V TTL quadrature encoder scales.

## Project Structure

- `rp2040-firmware/` - RP2040 firmware
  - `main.cpp` - Main firmware entry point
  - `position.cpp/h` - Position tracking implementation
  - `usb_device.cpp/h` - USB device implementation
  - `tusb_config.h` - TinyUSB configuration
  - `CMakeLists.txt` - CMake build configuration
  - `pico-sdk/` - Pico SDK submodule
- `linuxcnc-hal/` - LinuxCNC HAL component
  - `rp2040_encoder.comp` - HAL component source
  - `README.md` - Installation and usage instructions
  - `rp2040-encoder-example.hal` - Example HAL configuration
- `test_usb_device.py` - Python test script for USB communication

## Build Commands

### Firmware Build
```bash
cd rp2040-firmware
mkdir -p build
cd build
cmake ..
make
```

## Development Notes

- The project uses the RP2040 Pico SDK as a git submodule
- Firmware implements USB device functionality for LinuxCNC communication
- Position tracking is handled through dedicated position modules
- Quadrature encoders are read using PIO state machines for high-speed, accurate counting
- 32-bit signed counters for position tracking
- Simple, clear interfaces focused on the specific use case

## Hardware Configuration

### DRO Scale Connections
The system supports 4 DRO scales with TTL level A/B quadrature signals connected to GPIO pins 0-7, plus optional reference (R / index) pulse inputs on GPIO pins 26-29:
- Encoder 0 (X-axis): GPIO 0 (A), GPIO 1 (B), GPIO 26 (R)
- Encoder 1 (Y-axis): GPIO 2 (A), GPIO 3 (B), GPIO 27 (R)
- Encoder 2 (Z-axis): GPIO 4 (A), GPIO 5 (B), GPIO 28 (R)
- Encoder 3 (A-axis): GPIO 6 (A), GPIO 7 (B), GPIO 29 (R)

### Encoder Scaling
Scale factors convert encoder counts to meaningful units. Configure in main.cpp:
- Linear axes (X,Y,Z): Default 0.001 mm/count (1000 counts/mm)
- Rotary axis (A): Default 0.1 degrees/count (10 counts/degree)

### Position Counters
- PIO hardware counters are 32-bit signed integers
- Direct reading from PIO state machines without software extension
- Range: ±2,147,483,647 counts

## Testing

### USB Encoder Interface Testing
```bash
# Install pyusb for testing
pip install pyusb

# Run the test script (requires sudo on Linux)
python3 test_usb_device.py
```

The test script will:
1. Find the USB device (VID: 0x2E8A, PID: 0xC0DE)
2. Request a single position reading
3. Stream position data for 5 seconds

The device identifies itself with a descriptive serial number in the format:
`{N}ENC-{git_hash}-{build_date}` (e.g., "4ENC-2d49ae7-2025-06-17")

## Deployment

### Flashing the RP2040
1. Hold the BOOTSEL button while connecting the RP2040 board to USB
2. The RP2040 will appear as a mass storage device
3. Copy the generated `build/rp2040-hal-encoder.uf2` file to the device
4. The RP2040 will automatically reboot and run the firmware

### LinuxCNC HAL Component
See `linuxcnc-hal/README.md` for detailed installation and usage instructions.

Quick start:
```bash
cd linuxcnc-hal
sudo halcompile --install rp2040_encoder.comp
```

## USB Protocol

The device implements a vendor-specific USB interface with the following commands:

Every response is 56 bytes: a 4-byte sentinel, then the payload, zero-padded. Responses stay
below the endpoint's 64-byte packet on purpose (a full packet would be followed by a
zero-length packet). `rp2040-firmware/README.md` has the byte layouts.

- **0x01** - Get Position: 4 doubles with current position values, then the reference enable, invert, live and index-armed masks (1 byte each) and 4 uint32 reference pulse counts
- **0x02** - Set Test Mode: Sets test mode (requires 1 byte parameter)
  - 0 = Disable test mode (use real encoder data)
  - 1 = Sine wave pattern
  - 2 = Circular motion pattern
  - 3 = Linear ramp pattern
  - 4 = Random walk pattern
- **0x03** - Set Scale: Sets scale factor for an encoder (requires 1 byte encoder index + 8 bytes double scale factor)
- **0x04** - Get Scale: 4 doubles with scale factors for all encoders
- **0x05** - Reset Encoder: Resets encoder position to zero (requires 1 byte encoder index)
- **0x06** - Get Version: zero-padded `git describe` text
- **0x07** - Bootsel: reboots into the UF2 bootloader (the device drops off the bus)
- **0x08** - Reset Config: erases the persisted scales and reference masks; takes effect on the next boot
- **0x09** - Set Reference Config: Sets per-axis R-pulse enable and invert bitmasks (`[0x09, enable_mask, invert_mask]`, persisted in flash; default `enable_mask=0x0F`, `invert_mask=0x00`)
- **0x0A** - Get Reference Position: Returns 56 bytes like every response (`[sentinel 0x9C4E2A71: 4][4 doubles ref_positions: 32][4 uint32 ref_counts: 16]`)
- **0x0B** - Arm Index: Arms or disarms LinuxCNC `index-enable` zero-on-next-R for an axis (`[0x0B, encoder_index, arm]`)

Position data format: 4 double-precision floating point values (little-endian)

## HAL pins

The component is `rp2040_encoder`; its pins are `rp2040-encoder.0.*` (halcompile turns the
underscore into a dash). Per encoder N: `position-N`, `scale-N`, `scale-fb-N`, `sign-N`,
`reset-N`, and for the reference pulse `ref-enable-N`, `ref-invert-N`, `ref-count-N`,
`ref-position-N`, `ref-state-N`, `index-enable-N`; plus `connected` and `test-mode`.
`linuxcnc-hal/README.md` describes each one.

## Test Mode

The firmware includes a test mode feature that simulates realistic quadrature encoder behavior for development and testing purposes.

### Test Patterns

- **0 - Sine Wave**: Sinusoidal motion on all axes with different frequencies and amplitudes
- **1 - Circular**: XY circular motion with Z oscillation and A rotation
- **2 - Linear Ramp**: Constant velocity motion on all axes
- **3 - Random Walk**: Random position changes simulating measurement noise

### Usage

Test mode can be controlled via USB commands or enabled in firmware:

```cpp
// Enable in firmware (main.cpp)
pos.enable_test_mode(true);
pos.set_test_pattern(0);  // 0=SINE_WAVE, 1=CIRCULAR, 2=LINEAR_RAMP, 3=RANDOM_WALK
```

Or via USB commands in the test script or HAL component:
```bash
python3 test_usb_device.py  # Now includes test mode demonstration
```