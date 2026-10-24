"""cmd_watchdog(와 straight_drive)의 속도 한계: 최대 전진 · 최대 후진 · 최대 회전.

로봇 없이 시험할 수 있게 ROS 없는 순수 함수다. 값은 `이름: 값` 줄로 된 작은 파일(~/robot_link/limits.yaml)에
두어 재시작 · 재부팅 뒤에도 대시보드가 정한 값이 남는다(YAML 라이브러리 없이 읽을 만큼 단순하게).
"""
import math
import os

# Pico 펌웨어가 |v| <= 0.35 m/s · |w| <= 0.8 rad/s 로 자르므로 그보다 큰 한계는 뜻이 없다.
FIRMWARE = {'max_forward': 0.35, 'max_reverse': 0.35, 'max_angular': 0.8}
DEFAULTS = {'max_forward': 0.25, 'max_reverse': 0.15, 'max_angular': 0.6}
DEFAULT_PATH = '~/robot_link/limits.yaml'


def check(name, value):
    """값이 그 한계로 쓸 수 있으면 None, 아니면 이유(set_parameters 가 거부할 때 쓴다)."""
    if name not in FIRMWARE:
        return f'unknown limit {name}'
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return f'{name} must be a number'
    if not 0.0 <= value <= FIRMWARE[name]:
        return f'{name} {value} outside 0..{FIRMWARE[name]}'
    return None


def clamp(v, w, limits):
    """(v, w) 를 한계로 자른다: 전진은 max_forward, 후진은 max_reverse, |w| 는 max_angular."""
    v = min(v, limits['max_forward']) if v >= 0 else max(v, -limits['max_reverse'])
    lw = limits['max_angular']
    return v, max(-lw, min(lw, w))


def load(path=DEFAULT_PATH):
    """파일의 한계. 파일이 없거나 줄이 틀리거나 범위 밖이면 그 값은 기본값."""
    out = dict(DEFAULTS)
    try:
        with open(os.path.expanduser(path), encoding='utf-8') as f:
            lines = f.readlines()
    except OSError:
        return out
    for line in lines:
        key, sep, val = line.split('#', 1)[0].partition(':')
        key = key.strip()
        if not sep or key not in FIRMWARE:
            continue
        try:
            num = float(val)
        except ValueError:
            continue
        if check(key, num) is None:
            out[key] = num
    return out


def save(limits, path=DEFAULT_PATH):
    """한계를 쓴다(임시 파일 + 이름 바꾸기 — 전원이 끊겨도 반쪽 파일이 남지 않게)."""
    path = os.path.expanduser(path)
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        f.write('# cmd_watchdog speed limits (m/s, rad/s) - written by the node, the dashboard sets them\n')
        for k in FIRMWARE:
            f.write(f'{k}: {limits[k]:.3f}\n')
    os.replace(tmp, path)
