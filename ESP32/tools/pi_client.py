"""TCP bench client: python pi_client.py ESP_IP [--run --setpoint 2].

Read-only monitoring plus STOP is the default. --run explicitly requests motor operation.
Use the simulation firmware first; close the HMI before using this single-client port.
"""
import argparse
import socket
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--setpoint", type=float, default=2)
    parser.add_argument("--seconds", type=float, default=20)
    args = parser.parse_args()
    if not 0 <= args.setpoint <= 30 or not 0 < args.seconds <= 3600:
        parser.error("setpoint: 0..30 m/s; seconds: >0..3600")
    with socket.create_connection((args.host, args.port), timeout=1) as link:
        link.settimeout(0.1)
        def send(command):
            link.sendall((command + "\n").encode("ascii"))
        buffer = bytearray()
        last_status = time.monotonic()
        try:
            send("STOP")
            time.sleep(0.1)
            if args.run:
                send(f"SETPOINT {args.setpoint}")
                send("START")
            deadline, ping_at = time.monotonic() + args.seconds, 0
            while time.monotonic() < deadline:
                if time.monotonic() - last_status > 1:
                    raise OSError("Controller telemetry lost")
                if time.monotonic() >= ping_at:
                    send("PING")
                    ping_at = time.monotonic() + 0.5
                try:
                    chunk = link.recv(4096)
                except socket.timeout:
                    continue
                if not chunk:
                    raise OSError("Controller disconnected")
                buffer.extend(chunk)
                if len(buffer) > 8192:
                    raise ValueError("Oversized telemetry")
                while b"\n" in buffer:
                    line, _, rest = buffer.partition(b"\n")
                    buffer = bytearray(rest)
                    text = line.decode("ascii")
                    print(text)
                    if text.startswith("STATUS "):
                        last_status = time.monotonic()
        finally:
            try:
                send("STOP")
            except OSError:
                pass  # Firmware/drive watchdogs handle a broken physical link.


if __name__ == "__main__":
    main()
