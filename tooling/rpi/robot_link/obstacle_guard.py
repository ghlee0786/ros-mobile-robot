"""LiDAR obstacle guard for cmd_watchdog: the fastest linear speed that can still stop before an obstacle.

Pure functions (no ROS) so they can be tested without a robot. The scan is in base_link (0 rad = front).
Returns from the robot's own frame/posts lie at 0.14-0.19 m, so readings below `self_range` are ignored.
"""
import math


def nearest_in_sector(ranges, angle_min, angle_inc, center, half_width, self_range=0.20):
    """Smallest valid range whose bearing is within half_width of center (rad); inf if none."""
    best = math.inf
    for i, r in enumerate(ranges):
        if not math.isfinite(r) or r < self_range:
            continue
        a = angle_min + i * angle_inc
        d = math.atan2(math.sin(a - center), math.cos(a - center))
        if abs(d) <= half_width and r < best:
            best = r
    return best


def allowed_speed(nearest, stop_dist, dec, latency):
    """Largest |v| that stops at stop_dist from an obstacle `nearest` m away, decelerating at dec (m/s^2)
    after `latency` s of reaction: v*latency + v^2/(2*dec) <= nearest - stop_dist."""
    room = nearest - stop_dist
    if room <= 0:
        return 0.0
    if not math.isfinite(room):
        return math.inf
    # v^2/(2 dec) + latency v - room = 0
    return -dec * latency + math.sqrt((dec * latency) ** 2 + 2 * dec * room)


def guard(v, scan, stop_dist, dec, latency, half_width):
    """Clamp linear speed v using scan = (ranges, angle_min, angle_inc). Forward checks the front sector,
    reverse the rear. Returns (clamped v, nearest distance in the travel direction)."""
    if abs(v) < 1e-6:
        return v, math.inf
    ranges, amin, ainc = scan
    near = nearest_in_sector(ranges, amin, ainc, 0.0 if v > 0 else math.pi, half_width)
    vmax = allowed_speed(near, stop_dist, dec, latency)
    return math.copysign(min(abs(v), vmax), v), near
