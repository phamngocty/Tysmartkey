import serial
import time
import sys

sys.stdout.reconfigure(encoding='utf-8')

# ReadSysPara: 0xEF 0x01 0xFF 0xFF 0xFF 0xFF 0x01 0x00 0x03 0x0F 0x00 0x13
# We can send this directly through ESP32 if ESP32 forwards or we can check via python
try:
    ser = serial.Serial('COM14', 115200, timeout=1)
    time.sleep(0.3)
    ser.reset_input_buffer()
    
    # Check if ESP32 responds
    ser.write(b'1\n')
    time.sleep(0.5)
    while ser.in_waiting:
        print(ser.readline().decode('utf-8', errors='replace').strip())
        
    ser.close()
except Exception as e:
    print(f"Error: {e}")
