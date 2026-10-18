# Runs inside Fusion: list the RPLidar_A3M1 sub-tree (to find the spinning head) and which mesh
# export formats this Fusion build's ExportManager offers.
import adsk.core
import adsk.fusion

app = adsk.core.Application.get()
design = adsk.fusion.Design.cast(app.activeProduct)
root = design.rootComponent


def bbox(b):
    mn, mx = b.minPoint, b.maxPoint
    return [round(mn.x, 2), round(mn.y, 2), round(mn.z, 2), round(mx.x, 2), round(mx.y, 2), round(mx.z, 2)]


def tree(occ, depth=0, out=None):
    out = [] if out is None else out
    out.append({"depth": depth, "name": occ.name, "visible": occ.isLightBulbOn,
                "bodies": [b.name for b in occ.component.bRepBodies],
                "bbox_cm": bbox(occ.boundingBox)})
    for c in occ.childOccurrences:
        tree(c, depth + 1, out)
    return out


lidar = [o for o in root.occurrences if o.component.name.startswith("RPLidar")]
em = design.exportManager
result = {
    "lidar_trees": [tree(o) for o in lidar],
    "export_options": sorted(n for n in dir(em) if n.startswith("create") and n.endswith("ExportOptions")),
}
result
