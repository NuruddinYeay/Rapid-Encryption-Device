import serial
import time
import subprocess

# =========================
# CONFIGURATION
# =========================
SERIAL_PORT = '/dev/red_lcd'  # Ensure this is your actual port!
BAUD_RATE = 9600

# Put your EXACT interface names here!
PORT1_INTERFACE = "enp1s0"   # Change this to your Ethernet name!
PORT2_INTERFACE = "enp2s0"    # Change this to your Wi-Fi name!

def get_ip(interface):
    try:
        # Bulletproof Linux command: 
        # -4 forces IPv4, -o puts it on one line, head -n 1 guarantees absolutely no newlines
        cmd = f"ip -4 -o addr show {interface} 2>/dev/null | awk '{{print $4}}' | cut -d/ -f1 | head -n 1"
        ip = subprocess.check_output(cmd, shell=True).decode('ascii').strip()
        
        # If the command returns anything, send it. If blank, send Disconnected.
        if ip:
            return ip
        else:
            return "Disconnected"
    except Exception:
        # If the interface doesn't exist or crashes, fail safely.
        return "Disconnected"

def main():
    print(f"Monitoring Port 1: {PORT1_INTERFACE}")
    print(f"Monitoring Port 2: {PORT2_INTERFACE}")
    print(f"Opening Serial on: {SERIAL_PORT}")

    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    except Exception as e:
        print(f"CRITICAL ERROR: Could not open {SERIAL_PORT}. {e}")
        return

    while True:
        try:
            ip1 = get_ip(PORT1_INTERFACE)
            ip2 = get_ip(PORT2_INTERFACE)

            # Create the exact string: "192.168.1.5,Disconnected,1\n"
            data_string = f"{ip1},{ip2},1\n"
            
            ser.write(data_string.encode('ascii'))
            print(f"Successfully Sent: {data_string.strip()}")

            time.sleep(2)
            
        except KeyboardInterrupt:
            print("\nExiting...")
            break
        except Exception as e:
            print(f"Minor Loop Error: {e}")
            time.sleep(2)

if __name__ == "__main__":
    main()
