#!/usr/bin/env python3
"""Relay ROS 2 topics between two domains through localhost TCP, one single-domain process per side.

The robot link (domain 7) is CycloneDDS over Wi-Fi; the PC-local navigation stack (domain 8) is FastDDS
with shared memory, because on WSL CycloneDDS loses large messages (14 KB scans) between local
processes, especially for best-effort readers like SLAM and the costmaps, while FastDDS delivers them.
Different RMWs cannot share a process, so each side runs on its own and serialized bytes cross TCP:

  split_relay.py out --port 7701 /scan:sensor_msgs/msg/LaserScan:best_effort /odom:nav_msgs/msg/Odometry
      subscribe in this process's domain, send every message to the "in" side
  split_relay.py in  --port 7701 /scan:sensor_msgs/msg/LaserScan /odom:nav_msgs/msg/Odometry
      publish what arrives in this process's domain
By default "out" connects to 127.0.0.1:port and "in" listens there. --listen ADDR / --connect HOST turn
either side around, so the robot side can run on the Pi itself (Foxy) and the PC side connect to it over
Wi-Fi: no Jazzy participant then joins the robot's domain, whose discovery data Foxy cannot parse (it
corrupts the Pi's node graph and kills rosapi / stalls rosbridge).
Topic spec: name:type[:reliable|best_effort][:volatile|transient_local]. Both sides list the same
topics in the same order (the frame carries the index). Latched (transient_local) topics are re-sent
when the connection comes back.
"""
import argparse
import importlib
import socket
import struct
import threading
import time

import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy

HEADER = struct.Struct('<HI')   # topic index, payload length


def parse_spec(spec):
    name, typ, *rest = spec.split(':')
    rel = 'best_effort' if 'best_effort' in rest else 'reliable'
    dur = 'transient_local' if 'transient_local' in rest else 'volatile'
    pkg, _, cls = typ.split('/')
    msg = getattr(importlib.import_module(f'{pkg}.msg'), cls)
    qos = QoSProfile(depth=5 if name != '/tf_static' else 1,
                     reliability=ReliabilityPolicy.BEST_EFFORT if rel == 'best_effort' else ReliabilityPolicy.RELIABLE,
                     durability=DurabilityPolicy.TRANSIENT_LOCAL if dur == 'transient_local' else DurabilityPolicy.VOLATILE)
    return {'name': name, 'msg': msg, 'qos': qos, 'latched': dur == 'transient_local'}


def recv_exact(sock, n):
    buf = b''
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError('closed')
        buf += chunk
    return buf


def serve_sockets(addr, port, on_socket):
    """Listen on addr:port and hand each accepted socket to on_socket(sock) (one peer at a time)."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((addr, port))
    srv.listen(1)
    while rclpy.ok():
        conn, peer = srv.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        on_socket(conn, peer)


def connect_socket(host, port):
    """Connect to host:port, retrying every second until it works."""
    while rclpy.ok():
        try:
            s = socket.create_connection((host, port), timeout=2)
            s.settimeout(None)
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            return s
        except OSError:
            time.sleep(1.0)


def run_out(node, port, topics, listen=None, connect='127.0.0.1'):
    state = {'sock': None, 'n': [0] * len(topics), 'latched': {}}
    lock = threading.Lock()

    def send(sock, idx, data):
        sock.sendall(HEADER.pack(idx, len(data)) + data)

    def attach(s, peer):
        with lock:
            try:
                for idx, data in state['latched'].items():
                    send(s, idx, data)
            except OSError:
                s.close()
                return
            if state['sock'] is not None:
                state['sock'].close()
            state['sock'] = s
        node.get_logger().info(f'connected {peer}:{port}')

    def dial():
        attach(connect_socket(connect, port), connect)

    def make_cb(idx, latched):
        def cb(data):
            with lock:
                if latched:
                    state['latched'][idx] = data
                s = state['sock']
                if s is None:
                    return
                try:
                    send(s, idx, data)
                    state['n'][idx] += 1
                except OSError:
                    state['sock'] = None
                    s.close()
                    if listen is None:
                        threading.Thread(target=dial, daemon=True).start()
        return cb

    for idx, t in enumerate(topics):
        node.create_subscription(t['msg'], t['name'], make_cb(idx, t['latched']), t['qos'], raw=True)
    node.create_timer(10.0, lambda: node.get_logger().info(
        'forwarded ' + ', '.join(f"{t['name']}={n}" for t, n in zip(topics, state['n']))))
    if listen is None:
        threading.Thread(target=dial, daemon=True).start()
    else:
        threading.Thread(target=serve_sockets, args=(listen, port, attach), daemon=True).start()
    rclpy.spin(node)


def run_in(node, port, topics, listen='127.0.0.1', connect=None):
    pubs = [node.create_publisher(t['msg'], t['name'], t['qos']) for t in topics]
    count = [0] * len(topics)

    def read(conn, peer):
        node.get_logger().info(f'sender connected {peer}')
        try:
            while True:
                idx, n = HEADER.unpack(recv_exact(conn, HEADER.size))
                pubs[idx].publish(recv_exact(conn, n))
                count[idx] += 1
        except (ConnectionError, OSError):
            conn.close()

    def dial():
        while rclpy.ok():
            read(connect_socket(connect, port), connect)
            time.sleep(1.0)

    node.create_timer(10.0, lambda: node.get_logger().info(
        'published ' + ', '.join(f"{t['name']}={n}" for t, n in zip(topics, count))))
    if connect is None:
        threading.Thread(target=serve_sockets, args=(listen, port, read), daemon=True).start()
    else:
        threading.Thread(target=dial, daemon=True).start()
    rclpy.spin(node)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('role', choices=['out', 'in'])
    ap.add_argument('--port', type=int, required=True)
    side = ap.add_mutually_exclusive_group()
    side.add_argument('--listen', metavar='ADDR', help='wait for the other side on ADDR:port')
    side.add_argument('--connect', metavar='HOST', help='connect to the other side at HOST:port')
    ap.add_argument('topics', nargs='+')
    args = ap.parse_args()
    topics = [parse_spec(s) for s in args.topics]
    rclpy.init()
    node = rclpy.create_node(f'split_relay_{args.role}_{args.port}')
    try:
        if args.role == 'out':
            run_out(node, args.port, topics, listen=args.listen, connect=args.connect or '127.0.0.1')
        else:
            run_in(node, args.port, topics, listen=args.listen or '127.0.0.1', connect=args.connect)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
