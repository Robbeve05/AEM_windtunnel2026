"""Voorbeeldclient: python pi_client.py /dev/serial0 --setpoint 10 --seconds 20.

Vereist pyserial. Test eerst met SIMULATION_MODE=1.
"""
import argparse
import time
import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("--setpoint", type=float, default=10)
    parser.add_argument("--seconds", type=float, default=20)
    args = parser.parse_args()
    if not 0 <= args.setpoint <= 30 or not 0 < args.seconds <= 3600:
        parser.error("setpoint: 0..30 m/s; seconds: >0..3600")
    with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=1) as link:
        def send(command):
            link.write((command + "\n").encode("ascii"))
            link.flush()

        # Laat firmware starten. STOP wordt eerst in een aparte regelcyclus verwerkt.
        time.sleep(2)
        try:
            send("STOP")
            time.sleep(0.1)
            send(f"SETPOINT {args.setpoint}")
            send("START")
            deadline = time.monotonic() + args.seconds
            ping_at = 0
            while time.monotonic() < deadline:
                if time.monotonic() >= ping_at:
                    send("PING")
                    ping_at = time.monotonic() + 0.5
                line = link.readline().decode("ascii", errors="replace").strip()
                if line:
                    print(line)
        finally:
            send("STOP")
            # Toon status na STOP; OK QUEUED alleen is geen bevestiging van uitvoering.
            until = time.monotonic() + 0.5
            while time.monotonic() < until:
                print(link.readline().decode("ascii", errors="replace").strip())


if __name__ == "__main__":
    main()
