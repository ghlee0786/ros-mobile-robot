# Runs inside Fusion (via fusion_cmd.py execute_code): summarise the active design so the robot's
# parts (chassis, wheels, LiDAR) and their joint axes can be mapped for the Qt 3D model.
import adsk.core
import adsk.fusion

app = adsk.core.Application.get()
doc = app.activeDocument
design = adsk.fusion.Design.cast(app.activeProduct)
root = design.rootComponent
units = design.unitsManager.defaultLengthUnits


def bbox(b):
    if not b:
        return None
    mn, mx = b.minPoint, b.maxPoint
    return [round(mn.x, 2), round(mn.y, 2), round(mn.z, 2), round(mx.x, 2), round(mx.y, 2), round(mx.z, 2)]


def occ_info(occ, depth):
    comp = occ.component
    return {
        "name": occ.name,
        "component": comp.name,
        "depth": depth,
        "visible": occ.isLightBulbOn,
        "bbox_cm": bbox(occ.boundingBox),
        "bodies": [{"name": b.name, "bbox_cm": bbox(b.boundingBox),
                    "appearance": b.appearance.name if b.appearance else None,
                    "material": b.material.name if b.material else None}
                   for b in comp.bRepBodies],
    }


occs = []


def walk(occurrences, depth):
    for occ in occurrences:
        occs.append(occ_info(occ, depth))
        walk(occ.childOccurrences, depth + 1)


walk(root.occurrences, 0)

joints = []
for coll in (root.allJoints, root.allAsBuiltJoints):
    for j in coll:
        jt = j.jointMotion.jointType if j.jointMotion else None
        item = {"name": j.name, "type": jt,
                "occ1": j.occurrenceOne.name if j.occurrenceOne else None,
                "occ2": j.occurrenceTwo.name if j.occurrenceTwo else None}
        try:
            if jt == adsk.fusion.JointTypes.RevoluteJointType:
                ax = j.jointMotion.rotationAxisVector
                item["axis"] = [round(ax.x, 3), round(ax.y, 3), round(ax.z, 3)] if ax else None
                geo = j.geometryOrOriginOne
                if hasattr(geo, "origin") and geo.origin:
                    o = geo.origin
                    item["origin_cm"] = [round(o.x, 2), round(o.y, 2), round(o.z, 2)]
        except Exception as e:  # noqa: BLE001 - report and continue
            item["error"] = str(e)
        joints.append(item)

result = {
    "document": doc.name,
    "units": units,
    "root_bodies": [{"name": b.name, "bbox_cm": bbox(b.boundingBox)} for b in root.bRepBodies],
    "root_bbox_cm": bbox(root.boundingBox),
    "occurrences": occs,
    "joints": joints,
}
result
