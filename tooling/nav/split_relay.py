#!/usr/bin/env python3
"""Relay ROS 2 topics between two domains through localhost TCP, one single-domain process per side.

The robot link (domain 7) is CycloneDDS over Wi-Fi; the PC-local navigation stack (domain 8) is FastDDS
with shared memory, because on WSL CycloneDDS loses large messages (14 KB scans) between local
processes, especially for best-effort readers like SLAM and the costmaps, while FastDDS delivers them.
Different RMWs cannot share a process, so each side runs on its own and serialized bytes cross TCP:

  split_relay.py out --port 7701 /scan:sensor_msgs/msg/LaserScan:best_effort /odom:nav_msgs/msg/Odometry
      subscribe in this process's domain, send every message to the "in" side
  split_relay.py in  --port 7701 /scan:sensor_msgs/msg/LaserScan /odom:nav_msgs/msg/Odometry
      listen on 127.0.0.1:port, publish what arrives in this process's domain
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


def run_out(node, port, topics):
    state = {'sock': None, 'n': [0] * len(topics), 'latched': {}}
    lock = threading.Lock()

    def send(sock, idx, data):
        sock.sendall(HEADER.pack(idx, len(data)) + data)

    def connect():
        while rclpy.ok():
            try:
                s = socket.create_connection(('127.0.0.1', port), timeout=2)
                s.settimeout(None)
                s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                with lock:
                    for idx, data in state['latched'].items():
                        send(s, idx, data)
                    state['sock'] = s
                node.get_logger().info(f'connected to :{port}')
                return
            except OSError:
                time.sleep(1.0)

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
                    threading.Thread(target=connect, daemon=True).start()
        return cb

    for idx, t in enumerate(topics):
        node.create_subscription(t['msg'], t['name'], make_cb(idx, t['latched']), t['qos'], raw=True)
    node.create_timer(10.0, lambda: node.get_logger().info(
        'forwarded ' + ', '.join(f"{t['name']}={n}" for t, n in zip(topics, state['n']))))
    threading.Thread(target=connect, daemon=True).start()
    rclpy.spin(node)


def run_in(node, port, topics):
    pubs = [node.create_publisher(t['msg'], t['name'], t['qos']) for t in topics]
    count = [0] * len(topics)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', port))
    srv.listen(1)

    def serve():
        while rclpy.ok():
            conn, _ = srv.accept()
            node.get_logger().info('sender connected')
            try:
                while True:
                    idx, n = HEADER.unpack(recv_exact(conn, HEADER.size))
                    pubs[idx].publish(recv_exact(conn, n))
                    count[idx] += 1
            except (ConnectionError, OSError):
                conn.close()

    node.create_timer(10.0, lambda: node.get_logger().info(
        'published ' + ', '.join(f"{t['name']}={n}" for t, n in zip(topics, count))))
    threading.Thread(target=serve, daemon=True).start()
    rclpy.spin(node)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('role', choices=['out', 'in'])
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('topics', nargs='+')
    args = ap.parse_args()
    topics = [parse_spec(s) for s in args.topics]
    rclpy.init()
    node = rclpy.create_node(f'split_relay_{args.role}_{args.port}')
    try:
        (run_out if args.role == 'out' else run_in)(node, args.port, topics)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
