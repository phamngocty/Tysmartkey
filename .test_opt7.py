import serial
import time
import sys

sys.stdout.reconfigure(encoding='utf-8')

try:
    ser = serial.Serial('COM14', 115200, timeout=1)
    time.sleep(0.5)
    ser.reset_input_buffer()
    
    # Send '7' to trigger image capture / stream
    ser.write(b'7\n')
    start = time.time()
    while time.time() - start < 10:
        line = ser.readline().decode('utf-8', errors='replace').strip()
        if line:
            print("LOG:", line)
        if "Hết thời gian" in line or "Đã nhận trọn vẹn" in line or "Cảm biến từ chối" in line:
            break
            
    ser.close()
except Exception as e:
    print(f"ERROR: {e}")
