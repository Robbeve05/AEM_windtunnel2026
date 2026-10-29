#!/usr/bin/env python3
"""Authenticated maintenance-only Ethernet OTA upload. Author: R.E. van Eekelen.

Does not configure or bypass the physical drive-isolation interlock.
Secret must be stored outside Git in ~/.config/windtunnel/ota_secret (chmod 600).
"""
import argparse
import hashlib
import hmac
import os
import pathlib
import socket
import sys

MAX_SIZE = 0x300000
def line(sock, maximum=160):
    data=bytearray()
    while len(data)<maximum:
        b=sock.recv(1)
        if not b:
            raise ConnectionError("ESP32 disconnected")
        if b==b"\n":
            return data.decode("ascii")
        data.extend(b)
    raise ValueError("ESP32 response too long")

def upload(host, firmware, secret, port=5001):
    path=pathlib.Path(firmware).resolve()
    size=path.stat().st_size
    if not 1 <= size <= MAX_SIZE:
        raise ValueError("Unexpected app image size; must fit 3 MiB OTA slot")
    with path.open("rb") as stream:
        if stream.read(1)!=b"\xe9":
            raise ValueError("Not an ESP-IDF application image")
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    secret_file=pathlib.Path(secret).expanduser()
    if secret_file.stat().st_mode & 0o077:
        raise PermissionError(f"Secret file must be chmod 600: {secret_file}")
    key=secret_file.read_bytes().strip()
    if len(key)<32:
        raise ValueError("Secret must contain >=32 random bytes")
    print(f"Target {host}:{port}; image {path.name} ({size} bytes), SHA256 {digest}")
    with socket.create_connection((host,port),timeout=5) as sock:
        sock.settimeout(5)
        greeting=line(sock)
        if not greeting.startswith("NONCE "):
            raise RuntimeError("ESP32 declined maintenance: "+greeting)
        nonce=greeting.removeprefix("NONCE ")
        if len(nonce)!=32 or any(c not in "0123456789abcdef" for c in nonce):
            raise ValueError("Invalid ESP32 nonce")
        message=f"WOTA1:{nonce}:{size}:{digest}".encode("ascii")
        mac=hmac.new(key,message,hashlib.sha256).hexdigest()
        sock.sendall(f"WOTA1 {size} {digest} {mac}\n".encode("ascii"))
        answer=line(sock)
        if answer!="READY":
            raise RuntimeError("ESP32 OTA refused: "+answer)
        with path.open("rb") as stream:
            sent=0
            while chunk:=stream.read(1024):
                sock.sendall(chunk)
                sent+=len(chunk)
        final=line(sock)
        if final!="OK REBOOT":
            raise RuntimeError("ESP32 image rejected: "+final)
    print(f"ESP32 accepted {sent} bytes and is rebooting.")
    print("Check native HMI telemetry, version, timing and physical outputs after reboot.")
if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host",required=True)
    parser.add_argument("--firmware",required=True)
    parser.add_argument("--secret",default="~/.config/windtunnel/ota_secret")
    args=parser.parse_args()
    try: upload(args.host,args.firmware,args.secret)
    except Exception as ex:
        print(f"OTA FAILED: {ex}",file=sys.stderr)
        sys.exit(1)
