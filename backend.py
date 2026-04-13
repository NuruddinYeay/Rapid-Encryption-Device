import serial
import socket
import subprocess
import time
import struct
import os

SERIAL_PORT = "/dev/ttyACM1"
BAUD_RATE = 115200

def get_ip(interface):
    try:
        cmd = f"ip -4 addr show {interface} | grep inet | awk '{{print $2}}' | cut -d/ -f1"
        output = subprocess.check_output(cmd, shell=True).decode('utf-8').strip()
        return output if output else "N/A"
    except Exception: return "N/A"

def check_usb():
    try:
        with open('/sys/bus/usb/devices/3-6/bDeviceClass', 'r') as f:
            return 1 if f.read().strip() else 0
    except: return 0

def calc_crc(packet_bytes):
    crc = 0
    for b in packet_bytes: crc ^= b
    return crc

def send_packet(ser, cmd_id, payload):
    header = struct.pack('BBB', 0xAA, cmd_id, len(payload))
    packet = header + payload
    crc = calc_crc(packet)
    ser.write(packet + struct.pack('B', crc))

def main():
    print(f"Starting Backend on {SERIAL_PORT}...")
    while not os.path.exists(SERIAL_PORT):
        print("Waiting for Arduino...")
        time.sleep(2)

    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        print("Connected!")
        
        while True:
            if ser.in_waiting >= 4:
                req = ser.read(ser.in_waiting)
                
                if req[0] == 0xAA:
                    cmd_id = req[1]
                    
                    if cmd_id == 0x01:
                        payload = struct.pack('BB', check_usb(), 0x00)
                        send_packet(ser, 0x01, payload)
                        print("Sent Service State")
                        
                    elif cmd_id == 0x02:
                        ip_str = f"L:{get_ip('enp1s0')},{get_ip('enp2s0')}"
                        payload = struct.pack('32sB', ip_str.encode('ascii'), 1)
                        send_packet(ser, 0x02, payload)
                        print("Sent Platform State")
                        
                    elif cmd_id == 0xFF:
                        send_packet(ser, 0xFF, bytes([0,0,0,0]))
                        print("Answered Ping")

            time.sleep(0.05)

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
