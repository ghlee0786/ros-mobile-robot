#!/usr/bin/env python3
"""Send one command to the Fusion360MCP add-in (localhost:9876) and print the JSON reply.

    python tooling/fusion/fusion_cmd.py <type> ['<params json>']
    python tooling/fusion/fusion_cmd.py execute_code --code-file script.py
The add-in must be running inside Fusion (Shift+S > Add-Ins > Fusion360MCP > Run).
"""
import json
import socket
import sys

HOST, PORT = "localhost", 9876


def call(cmd_type, params=None, timeout=60.0):
    with socket.create_connection((HOST, PORT), timeout=timeout) as s:
        s.sendall((json.dumps({"type": cmd_type, "params": params or {}}) + "\n").encode("utf-8"))
        buf = b""
        while b"\n" not in buf:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    return json.loads(buf.split(b"\n", 1)[0])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    if len(sys.argv) > 3 and sys.argv[2] == "--code-file":
        params = {"code": open(sys.argv[3], encoding="utf-8").read()}
    else:
        params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    try:
        print(json.dumps(call(cmd, params), ensure_ascii=False, indent=2))
    except ConnectionRefusedError:
        print("Fusion 애드인에 연결할 수 없습니다: Fusion에서 Shift+S > 애드인 > Fusion360MCP > 실행 하세요.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
