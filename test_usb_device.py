#!/usr/bin/env python3
"""
Test script for the RP2040 HAL DRO USB device.
Requires pyusb: pip install pyusb
"""

import usb.core
import usb.util
import struct
import time
import sys

# Protocol constants - source of truth: rp2040-firmware/usb_device.h
VENDOR_ID = 0x2E8A
PRODUCT_ID = 0xC0DE

VENDOR_REQUEST_GET_POSITION = 0x01
VENDOR_REQUEST_SET_TEST_MODE = 0x02
VENDOR_REQUEST_GET_SCALE = 0x04

POSITION_DATA_SENTINEL = 0x3F8A7C91
SCALE_DATA_SENTINEL = 0x7B2D4E8F

# Test patterns
TEST_PATTERN_SINE_WAVE = 0
TEST_PATTERN_CIRCULAR = 1
TEST_PATTERN_LINEAR_RAMP = 2
TEST_PATTERN_RANDOM_WALK = 3

# Endpoints
EP_IN = 0x81
EP_OUT = 0x01

def find_device():
    """Find the USB device"""
    dev = usb.core.find(idVendor=VENDOR_ID, idProduct=PRODUCT_ID)
    if dev is None:
        raise ValueError("Device not found")
    return dev

def setup_device(dev):
    """Setup the USB device"""
    try:
        # Set the active configuration
        dev.set_configuration()
        
        # Claim the vendor interface
        cfg = dev.get_active_configuration()
        intf = cfg[(0, 0)]
        
        # Check if kernel driver is active and detach if needed
        try:
            if dev.is_kernel_driver_active(intf.bInterfaceNumber):
                dev.detach_kernel_driver(intf.bInterfaceNumber)
        except usb.core.USBError:
            # On macOS, this might not be needed or supported
            pass
        
        # Claim the interface
        usb.util.claim_interface(dev, intf.bInterfaceNumber)
        
        return intf
    except usb.core.USBError as e:
        print(f"USB setup error: {e}")
        print("Try running with sudo or check USB permissions")
        raise

def get_position_once(dev):
    """Get position data once"""
    try:
        # Send request
        result = dev.write(EP_OUT, [VENDOR_REQUEST_GET_POSITION], timeout=100)
        if result != 1:
            return None
        
        # Wait for processing
        time.sleep(0.05)
        
        # Read response
        data = dev.read(EP_IN, 64, timeout=100)
        
        # Parse the data: [sentinel:4 bytes][positions:32 bytes] = 36 bytes total
        if len(data) >= 36:
            # Validate sentinel first
            sentinel = struct.unpack('<L', bytes(data[:4]))[0]
            if sentinel != POSITION_DATA_SENTINEL:
                print(f"Warning: Invalid position data sentinel 0x{sentinel:08X}, expected 0x{POSITION_DATA_SENTINEL:08X}")
                # Clear USB read queue to remove any stale data
                try:
                    while True:
                        dev.read(EP_IN, 64, timeout=1)
                except usb.core.USBTimeoutError:
                    pass  # Queue is now empty
                return None
            
            # Extract position data after sentinel
            positions = struct.unpack('<4d', bytes(data[4:36]))
            return positions
        elif len(data) > 0:
            print(f"Warning: Received {len(data)} bytes, expected 36")
            
    except usb.core.USBTimeoutError:
        pass  # Timeout is normal, just return None
    except Exception as e:
        print(f"USB error: {e}")
        
    return None

def get_position_fast(dev):
    """Get position data with minimal delays for high-frequency testing"""
    try:
        # Send request with short timeout
        result = dev.write(EP_OUT, [VENDOR_REQUEST_GET_POSITION], timeout=10)
        if result != 1:
            return None
        
        # No artificial delay - let USB handle timing
        
        # Read response with short timeout
        data = dev.read(EP_IN, 64, timeout=10)
        
        # Parse the data: [sentinel:4 bytes][positions:32 bytes] = 36 bytes total
        if len(data) >= 36:
            # Validate sentinel first
            sentinel = struct.unpack('<L', bytes(data[:4]))[0]
            if sentinel != POSITION_DATA_SENTINEL:
                # Clear USB read queue to remove any stale data (silently in fast mode)
                try:
                    while True:
                        dev.read(EP_IN, 64, timeout=1)
                except usb.core.USBTimeoutError:
                    pass  # Queue is now empty
                return None  # Invalid data, silently ignore in fast mode
            
            # Extract position data after sentinel
            positions = struct.unpack('<4d', bytes(data[4:36]))
            return positions
            
    except usb.core.USBTimeoutError:
        pass  # Timeout is normal, just return None
    except Exception as e:
        sys.stderr.write(f"USB error in fast read: {e}\n")
        
    return None

def get_scale_data(dev):
    """Get scale data with sentinel validation"""
    try:
        # Send request
        result = dev.write(EP_OUT, [VENDOR_REQUEST_GET_SCALE], timeout=100)
        if result != 1:
            return None
        
        # Wait for processing
        time.sleep(0.05)
        
        # Read response
        data = dev.read(EP_IN, 64, timeout=100)
        
        # Parse the data: [sentinel:4 bytes][scales:32 bytes] = 36 bytes total
        if len(data) >= 36:
            # Validate sentinel first
            sentinel = struct.unpack('<L', bytes(data[:4]))[0]
            if sentinel != SCALE_DATA_SENTINEL:
                print(f"Warning: Invalid scale data sentinel 0x{sentinel:08X}, expected 0x{SCALE_DATA_SENTINEL:08X}")
                # Clear USB read queue to remove any stale data
                try:
                    while True:
                        dev.read(EP_IN, 64, timeout=1)
                except usb.core.USBTimeoutError:
                    pass  # Queue is now empty
                return None
            
            # Extract scale data after sentinel
            scales = struct.unpack('<4d', bytes(data[4:36]))
            return scales
        elif len(data) > 0:
            print(f"Warning: Received {len(data)} bytes, expected 36")
            
    except usb.core.USBTimeoutError:
        pass  # Timeout is normal, just return None
    except Exception as e:
        print(f"USB error: {e}")
        
    return None

def poll_positions(dev, duration=5, frequency=2):
    """Poll position data at specified frequency"""
    print(f"Polling positions for {duration} seconds at {frequency} Hz...")
    
    start_time = time.time()
    count = 0
    successful_reads = 0
    interval = 1.0 / frequency
    
    while time.time() - start_time < duration:
        positions = get_position_once(dev)
        count += 1
        
        if positions:
            successful_reads += 1
            print(f"[{successful_reads:4d}] X:{positions[0]:8.3f} Y:{positions[1]:8.3f} Z:{positions[2]:8.3f} A:{positions[3]:8.1f}")
        else:
            print(f"[{count:4d}] No response")
        
        time.sleep(interval)
    
    success_rate = (successful_reads / count) * 100 if count > 0 else 0
    print(f"Success rate: {successful_reads}/{count} ({success_rate:.1f}%)")

def poll_high_frequency(dev, duration=5):
    """Poll position data as fast as possible with inline display"""
    print(f"High-frequency polling for {duration} seconds (no artificial delays)...")
    print("(Press Ctrl+C to stop early)")
    
    start_time = time.time()
    count = 0
    successful_reads = 0
    last_update_time = start_time
    
    try:
        while time.time() - start_time < duration:
            loop_start = time.time()
            positions = get_position_fast(dev)
            count += 1
            
            current_time = time.time()
            elapsed = current_time - start_time
            
            if positions:
                successful_reads += 1
                
            # Update display every 0.1 seconds to avoid overwhelming the terminal
            if current_time - last_update_time >= 0.1:
                actual_pps = count / elapsed if elapsed > 0 else 0
                successful_pps = successful_reads / elapsed if elapsed > 0 else 0
                avg_loop_time = (current_time - loop_start) * 1000  # Convert to ms
                
                if positions:
                    print(f"\r[{successful_reads:5d}/{count:5d}] {elapsed:6.2f}s | X:{positions[0]:8.3f} Y:{positions[1]:8.3f} Z:{positions[2]:8.3f} A:{positions[3]:8.1f} | {actual_pps:6.1f} pps | {avg_loop_time:4.1f}ms/loop", end="", flush=True)
                else:
                    print(f"\r[{successful_reads:5d}/{count:5d}] {elapsed:6.2f}s | No response                                                    | {actual_pps:6.1f} pps | {avg_loop_time:4.1f}ms/loop", end="", flush=True)
                
                last_update_time = current_time
            
    except KeyboardInterrupt:
        elapsed = time.time() - start_time
        print(f"\n\nStopped by user after {elapsed:.2f} seconds")
    
    elapsed = time.time() - start_time
    actual_pps = count / elapsed if elapsed > 0 else 0
    successful_pps = successful_reads / elapsed if elapsed > 0 else 0
    
    print(f"\nPerformance Summary:")
    print(f"  Total packets attempted: {count}")
    print(f"  Successful packets: {successful_reads}")
    print(f"  Success rate: {(successful_reads/count)*100 if count > 0 else 0:.1f}%")
    print(f"  Actual packets per second: {actual_pps:.1f}")
    print(f"  Successful packets per second: {successful_pps:.1f}")
    print(f"  Average time per attempt: {(elapsed/count)*1000 if count > 0 else 0:.1f} ms")

def test_mode_demo(dev):
    """Demonstrate test mode functionality"""
    print("\nTesting test mode functionality:")
    
    patterns = [
        (TEST_PATTERN_SINE_WAVE, "Sine Wave"),
        (TEST_PATTERN_CIRCULAR, "Circular Motion"),
        (TEST_PATTERN_LINEAR_RAMP, "Linear Ramp"),
        (TEST_PATTERN_RANDOM_WALK, "Random Walk")
    ]
    
    for pattern_id, pattern_name in patterns:
        print(f"\n--- Testing {pattern_name} Pattern ---")
        
        # Set test mode and pattern
        try:
            dev.write(EP_OUT, [VENDOR_REQUEST_SET_TEST_MODE, pattern_id + 1], timeout=1000)
            time.sleep(0.2)
            
            # Read several positions to show the pattern
            successful_reads = 0
            for i in range(8):
                positions = get_position_once(dev)
                if positions:
                    successful_reads += 1
                    print(f"[{i+1:2d}] X:{positions[0]:8.3f} Y:{positions[1]:8.3f} Z:{positions[2]:8.3f} A:{positions[3]:8.1f}")
                else:
                    print(f"[{i+1:2d}] No response")
                time.sleep(0.3)
            
            print(f"Pattern success rate: {successful_reads}/8")
            
        except Exception as e:
            print(f"Error testing {pattern_name}: {e}")
    
    # Disable test mode
    print("\nDisabling test mode...")
    try:
        dev.write(EP_OUT, [VENDOR_REQUEST_SET_TEST_MODE, 0], timeout=1000)
        time.sleep(0.2)
        
        # Verify test mode is off (should return to encoder data or zeros)
        positions = get_position_once(dev)
        if positions:
            print(f"Final positions: X:{positions[0]:8.3f} Y:{positions[1]:8.3f} Z:{positions[2]:8.3f} A:{positions[3]:8.1f}")
        else:
            print("No response after disabling test mode")
    except Exception as e:
        print(f"Error disabling test mode: {e}")

def main():
    try:
        # Find and setup device
        print("Looking for RP2040 HAL DRO device...")
        dev = find_device()
        # Get device serial number
        try:
            serial = usb.util.get_string(dev, dev.iSerialNumber)
            print(f"Found device: {dev}")
            print(f"Serial: {serial}")
        except:
            print(f"Found device: {dev}")
            print("Serial: Unable to read")
        
        intf = setup_device(dev)
        print(f"Device configured, interface: {intf.bInterfaceNumber}")
        
        # Test single position read
        print("\nTesting single position read:")
        positions = get_position_once(dev)
        if positions:
            print(f"Positions: X:{positions[0]:8.3f} Y:{positions[1]:8.3f} Z:{positions[2]:8.3f} A:{positions[3]:8.1f}")
        else:
            print("No response received")
        
        # Test scale data read
        print("\nTesting scale data read:")
        scales = get_scale_data(dev)
        if scales:
            print(f"Scales: X:{scales[0]:8.6f} Y:{scales[1]:8.6f} Z:{scales[2]:8.6f} A:{scales[3]:8.6f}")
        else:
            print("No scale response received")
        
        # Test position polling at a reasonable rate
        print("\nTesting position polling:")
        poll_positions(dev, duration=10, frequency=2)  # 2 Hz for 10 seconds
        
        # Test high-frequency polling
        print("\nTesting high-frequency polling:")
        poll_high_frequency(dev, duration=10)  # 1ms intervals for 10 seconds
        
        # Test all test mode patterns
        test_mode_demo(dev)
        
    except usb.core.USBError as e:
        print(f"USB Error: {e}")
        print("\nTroubleshooting tips:")
        print("1. Try running with sudo: sudo python3 test_usb_device.py")
        print("2. Make sure the RP2040 is running the correct firmware")
        print("3. Try unplugging and reconnecting the device")
        print("4. Check if another process is using the device")
        sys.exit(1)
    except ValueError as e:
        print(f"Device Error: {e}")
        print("\nTroubleshooting tips:")
        print("1. Make sure the RP2040 board is connected via USB")
        print("2. Verify the firmware is flashed correctly")
        print("3. Check the USB cable connection")
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()