"""Capture serial output from the C6 for a fixed duration, then exit.

Usage: python read_serial.py [COMx] [seconds]
"""
import sys
import time

import serial
from serial.tools import list_ports


def pick_port(arg=None):
    if arg:
        return arg
    ports = [p.device for p in list_ports.comports()]
    return ports[0] if ports else None


def main():
    port = pick_port(sys.argv[1] if len(sys.argv) > 1 else None)
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0
    if not port:
        print("No serial port found")
        return 1
    print(f"--- reading {port} @115200 for {seconds:.0f}s ---", flush=True)
    # On the C6 native USB-Serial/JTAG, DTR/RTS drive EN(reset) and boot.
    # Open with both de-asserted so we don't hold the chip in reset.
    p = serial.Serial(timeout=0.5)
    p.port = port
    p.baudrate = 115200
    # RTS drives EN(reset): keep False so we don't hold the chip in reset.
    # DTR marks the HWCDC host "connected" so it actually flushes TX: keep True.
    p.dtr = True
    p.rts = False
    p.open()
    with p:
        end = time.time() + seconds
        while time.time() < end:
            line = p.readline()
            if line:
                sys.stdout.write(line.decode("utf-8", "replace"))
                sys.stdout.flush()
    print("--- done ---", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
