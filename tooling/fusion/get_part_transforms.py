# Runs inside Fusion: world transforms of the moving parts' occurrences, so their per-occurrence OBJ
# exports (written in component-local coordinates) can be placed in the assembly's world frame.
# Each matrix is checked by mapping the component-local bounding box and comparing with the
# occurrence's world bounding box. OUT_FILE is replaced by the caller.
import json

import adsk.core
import adsk.fusion

OUT_FILE = r"__OUT_FILE__"
NAMES = ["Wheel:1", "Wheel_Tire:1", "Wheel_Axis:1", "Wheel:2", "Wheel_Tire:2", "Wheel_Axis:2", "KINETIC_ASM:1"]

app = adsk.core.Application.get()
design = adsk.fusion.Design.cast(app.activeProduct)
by_name = {o.name: o for o in design.rootComponent.allOccurrences}


def corners(bb):
    mn, mx = bb.minPoint, bb.maxPoint
    return [adsk.core.Point3D.create(x, y, z) for x in (mn.x, mx.x) for y in (mn.y, mx.y) for z in (mn.z, mx.z)]


out = {}
for name in NAMES:
    occ = by_name[name]
    m = occ.transform2
    comp_bb = occ.component.boundingBox
    pts = corners(comp_bb)
    for p in pts:
        p.transformBy(m)
    xs, ys, zs = [p.x for p in pts], [p.y for p in pts], [p.z for p in pts]
    mapped = [min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)]
    wb = occ.boundingBox
    world = [wb.minPoint.x, wb.minPoint.y, wb.minPoint.z, wb.maxPoint.x, wb.maxPoint.y, wb.maxPoint.z]
    out[name] = {
        "matrix_cm": list(m.asArray()),              # row-major 4x4, translation in cm
        "check_max_err_cm": round(max(abs(a - b) for a, b in zip(mapped, world)), 3),
    }
with open(OUT_FILE, "w", encoding="utf-8") as f:
    json.dump(out, f, indent=1)
{k: v["check_max_err_cm"] for k, v in out.items()}
