# Runs inside Fusion (fusion_cmd.py execute_code): export the MobileRobot assembly as separate OBJ
# meshes for the dashboard — the parts that move get their own file so the app can spin them:
#   body.obj        everything visible that does not move relative to the chassis
#   wheel_a.obj     Wheel:1 + Wheel_Tire:1 + Wheel_Axis:1   (Fusion x < 0 side)
#   wheel_b.obj     Wheel:2 + Wheel_Tire:2 + Wheel_Axis:2   (Fusion x > 0 side)
#   lidar_head.obj  RPLidar_A3M1 / KINETIC_ASM (the spinning head of the RPLIDAR A3)
# plus parts.json with each moving part's pivot and axis, all in the design's world frame (mm).
# OUT_DIR is replaced by the caller.
import json
import os

import adsk.core
import adsk.fusion

OUT_DIR = r"__OUT_DIR__"

app = adsk.core.Application.get()
design = adsk.fusion.Design.cast(app.activeProduct)
root = design.rootComponent
em = design.exportManager
os.makedirs(OUT_DIR, exist_ok=True)

by_name = {o.name: o for o in root.allOccurrences}
groups = {
    "wheel_a": ["Wheel:1", "Wheel_Tire:1", "Wheel_Axis:1"],
    "wheel_b": ["Wheel:2", "Wheel_Tire:2", "Wheel_Axis:2"],
    "lidar_head": ["KINETIC_ASM:1"],
}
missing = [n for names in groups.values() for n in names if n not in by_name]


def mm(v):
    return round(v * 10.0, 2)  # Fusion internal length unit is cm


def bbox_of(occs):
    xs, ys, zs = [], [], []
    for o in occs:
        b = o.boundingBox
        xs += [b.minPoint.x, b.maxPoint.x]
        ys += [b.minPoint.y, b.maxPoint.y]
        zs += [b.minPoint.z, b.maxPoint.z]
    return [mm(min(xs)), mm(min(ys)), mm(min(zs)), mm(max(xs)), mm(max(ys)), mm(max(zs))]


def export_obj(geometry, path):
    opts = em.createOBJExportOptions(geometry, path)
    opts.unitType = adsk.fusion.DistanceUnits.MillimeterDistanceUnits
    opts.meshRefinement = adsk.fusion.MeshRefinementSettings.MeshRefinementMedium
    return em.execute(opts)


report = {"out_dir": OUT_DIR, "missing": missing, "files": {}, "parts": {}}
if not missing:
    # Moving parts, one file each.
    for key, names in groups.items():
        occs = [by_name[n] for n in names]
        tmp = []
        for i, o in enumerate(occs):
            p = os.path.join(OUT_DIR, f"{key}__{i}.obj")
            export_obj(o, p)
            tmp.append(p)
        report["files"][key] = tmp
        b = bbox_of(occs)
        centre = [round((b[i] + b[i + 3]) / 2, 2) for i in range(3)]
        report["parts"][key] = {"bbox_mm": b, "pivot_mm": centre,
                                "axis": [1, 0, 0] if key.startswith("wheel") else [0, 1, 0]}

    # Body = whole assembly with the moving parts hidden for the export, then restored.
    moving = [by_name[n] for names in groups.values() for n in names]
    saved = [(o, o.isLightBulbOn) for o in moving]
    try:
        for o in moving:
            o.isLightBulbOn = False
        body_path = os.path.join(OUT_DIR, "body.obj")
        export_obj(root, body_path)
        report["files"]["body"] = [body_path]
    finally:
        for o, on in saved:
            o.isLightBulbOn = on

    tire = by_name["Wheel_Tire:1"].boundingBox
    report["ground_y_mm"] = mm(tire.minPoint.y)
    report["frame"] = "Fusion world, millimetres, +Y up; wheel axles along X; robot front/back along Z"
    with open(os.path.join(OUT_DIR, "parts.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
report
