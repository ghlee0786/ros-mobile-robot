#!/usr/bin/env python3
"""대시보드 3D 로봇 모델(dashboard/model/*.obj) 의 삼각형 수를 줄여 같은 이름의 .glb 로 쓴다.

    UV_CACHE_DIR=E:\\Temp\\uv-cache uv run --no-project --with numpy --with trimesh \\
        --with pymeshlab python tooling/fusion/decimate_model.py \\
        [--target-body 60000] [--target-lidar 15000] [--target-wheel 6000] [--model-dir dashboard/model]

prepare_dashboard_model.py 가 만든 OBJ(m 단위, glTF 좌표) 를 읽어 .mtl 의 재질(Kd 색)별로 메시를
나누고, 재질 안의 이어진 조각마다 삼각형 수에 비례한 몫으로 MeshLab quadric edge collapse(pymeshlab)
로 줄인 뒤 법선을 다시 계산해 body.glb / lidar_head.glb / wheel_left.glb / wheel_right.glb 로 쓴다.
좌표는 바꾸지 않는다. (fast-simplification 은 Fusion 판재 · 압출재의 가늘고 긴 삼각형에서 축약을
멈춰 body 가 목표의 2.5 배에 머물러서 pymeshlab 을 쓴다.)

대시보드(dashboard/src/robot.cpp)는 같은 이름의 glb -> gltf -> fbx -> obj 순서로 먼저 있는 파일을
읽으므로 glb 가 있으면 그것을 쓴다. 원본 OBJ/MTL 은 지우거나 바꾸지 않는다 — glb 를 지우면
대시보드는 원래대로 OBJ 를 쓴다.
"""
import argparse
import os
import sys
import time

import numpy as np
import pymeshlab
import trimesh

SMOOTH_ANGLE = np.radians(30)  # 이보다 꺾인 모서리는 법선을 나눠 CAD 의 각진 면을 살린다
MAX_BOX_ERR = 0.001  # 파일 경계 상자에 닿는 조각 면이 이만큼(m) 넘게 줄면 덜 줄인다
PART_BOX_ERR = 0.15  # 안쪽 조각은 제 크기의 이 비율까지 줄어도 된다(작은 부품이 통째로 사라지는 것만 막는다)
MIN_TRIS = 12  # 조각마다 남기는 최소 삼각형 수(상자 하나)


def read_mtl(path):
    """newmtl 이름 -> Kd 색 (r, g, b)."""
    colors, cur = {}, None
    if os.path.exists(path):
        for line in open(path, encoding="utf-8", errors="replace"):
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "newmtl":
                cur = line.split(None, 1)[1].strip()
                colors[cur] = (0.8, 0.8, 0.8)
            elif parts[0] == "Kd" and cur is not None:
                colors[cur] = tuple(float(t) for t in parts[1:4])
    return colors


def read_obj(path):
    """OBJ -> (정점 배열, {재질 이름: 삼각형 정점 인덱스 리스트}). 다각형은 부채꼴로 나눈다."""
    verts, faces, cur = [], {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("v "):
            verts.append(line.split()[1:4])
        elif line.startswith("f "):
            idx = [int(t.split("/")[0]) for t in line.split()[1:]]
            idx = [i - 1 if i > 0 else len(verts) + i for i in idx]
            tri = faces.setdefault(cur, [])
            for k in range(1, len(idx) - 1):
                tri.append((idx[0], idx[k], idx[k + 1]))
        elif line.startswith("usemtl "):
            cur = line.split(None, 1)[1].strip()
    return np.array(verts, dtype=np.float64), {m: np.array(f, dtype=np.int64) for m, f in faces.items()}


def merge(vertices, faces):
    """같은 위치(0.1 µm 단위)의 정점을 합치고 쓰이지 않는 정점과 넓이 0 인 면을 버린다."""
    keys, inverse = np.unique(np.round(vertices[faces.ravel()] * 1e7).astype(np.int64), axis=0,
                              return_inverse=True)
    faces = inverse.reshape(-1, 3)
    faces = faces[(faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & (faces[:, 0] != faces[:, 2])]
    return keys / 1e7, faces


def shade(vertices, faces):
    """넓이 가중 정점 법선. 면 법선과 SMOOTH_ANGLE 넘게 벌어지는 꼭짓점은 면 법선을 써서 모서리를 살린다."""
    cross = np.cross(vertices[faces[:, 1]] - vertices[faces[:, 0]], vertices[faces[:, 2]] - vertices[faces[:, 0]])
    keep = np.linalg.norm(cross, axis=1) > 0
    faces, cross = faces[keep], cross[keep]
    fn = cross / np.linalg.norm(cross, axis=1, keepdims=True)
    vn = np.zeros_like(vertices)
    for k in range(3):
        np.add.at(vn, faces[:, k], cross)
    vn /= np.maximum(np.linalg.norm(vn, axis=1, keepdims=True), 1e-30)
    corner_fn = np.repeat(fn, 3, axis=0)
    corner_vn = vn[faces.ravel()]
    sharp = (corner_vn * corner_fn).sum(1) < np.cos(SMOOTH_ANGLE)
    normals = np.where(sharp[:, None], corner_fn, corner_vn)
    # 꼭짓점마다 (정점, 법선) 이 같은 것끼리 다시 합친다
    keys = np.column_stack([faces.ravel(), np.round(normals * 1e4)])
    _, first, inverse = np.unique(keys, axis=0, return_index=True, return_inverse=True)
    corners = faces.ravel()[first]
    return vertices[corners], normals[first], inverse.reshape(-1, 3)


def split(vertices, faces):
    """같은 위치 정점을 합친 뒤(이웃 면이 이어져야 줄일 수 있다) 이어진 조각마다 (정점, 면) 으로 나눈다."""
    v, f = merge(vertices, faces)
    label = np.arange(len(v))
    while True:  # 면의 세 정점에 가장 작은 번호를 퍼뜨리고 포인터를 건너뛰어 조각 번호를 정한다
        low = label[f].min(1)
        new = label.copy()
        for k in range(3):
            np.minimum.at(new, f[:, k], low)
        new = new[new]
        if np.array_equal(new, label):
            break
        label = new
    face_label = label[f[:, 0]]
    order = np.argsort(face_label, kind="stable")
    cuts = np.flatnonzero(np.diff(face_label[order])) + 1
    return [merge(v, f[part]) for part in np.split(order, cuts)]


def reduce(v, f, share, file_box):
    """한 조각을 share 개 근처로 줄인다. 조각의 경계 상자가 허용치 넘게 줄면(작은 조각이 사라지는 경우)
    share 를 두 배씩 늘려 다시 하고, 끝내 안 되면 원본을 쓴다. 허용치는 파일 전체 경계 상자에 닿는
    면에서 MAX_BOX_ERR, 안쪽 면에서는 조각 크기의 PART_BOX_ERR 배(최소 MAX_BOX_ERR).

    MeshLab quadric edge collapse 를 preservenormal 로 돌려 면이 뒤집히는 축약을 하지 않으므로 원본의
    감김 방향(바깥쪽) 이 유지된다."""
    box = np.array([v.min(0), v.max(0)])
    tol = np.where(np.abs(box - file_box) < 1e-6, MAX_BOX_ERR,
                   max(MAX_BOX_ERR, PART_BOX_ERR * (box[1] - box[0]).max()))
    while share < len(f):
        ms = pymeshlab.MeshSet()
        ms.add_mesh(pymeshlab.Mesh(v, f.astype(np.int32)))
        ms.meshing_decimation_quadric_edge_collapse(targetfacenum=share, preservenormal=True, planarquadric=True)
        rv, rf = merge(ms.current_mesh().vertex_matrix(), ms.current_mesh().face_matrix().astype(np.int64))
        if len(rf) and (np.abs(np.array([rv.min(0), rv.max(0)]) - box) <= tol).all():
            return rv, rf
        share *= 2
    return v, f


def decimate(obj_path, target, glb_path):
    """obj_path 를 재질별 · 조각별로 줄여 glb_path 에 쓴다. 결과 수치(dict) 를 돌려준다.

    몫은 조각의 삼각형 수에 비례한다. 더 줄지 않는 조각(이미 단순한 압출 프로파일 등) 이 남기면
    그만큼을 빼고 나머지 조각의 비율을 다시 정해 몇 번 더 돌린다."""
    t0 = time.time()
    vertices, groups = read_obj(obj_path)
    colors = read_mtl(os.path.splitext(obj_path)[0] + ".mtl")
    total = sum(len(f) for f in groups.values())
    parts = [(name, v, f) for name, faces in groups.items() for v, f in split(vertices, faces)]
    used = vertices[np.unique(np.concatenate(list(groups.values())))]
    before = np.array([used.min(0), used.max(0)])
    done, scale = {}, target / total
    for _ in range(4):
        result = {i: reduce(v, f, max(min(len(f), MIN_TRIS), round(scale * len(f))), before)
                  for i, (_, v, f) in enumerate(parts) if i not in done}
        result.update(done)
        count = sum(len(r[1]) for r in result.values())
        if abs(count - target) <= 0.1 * target:
            break
        # 몫보다 훨씬 많이 남은 조각과 최소치에 걸린 조각은 고정하고 나머지의 비율을 다시 정한다
        done = {i: r for i, r in result.items()
                if i in done or len(r[1]) > 1.2 * scale * len(parts[i][2]) or len(r[1]) <= MIN_TRIS}
        free = sum(len(parts[i][2]) for i in result if i not in done)
        budget = target - sum(len(r[1]) for r in done.values())
        if free == 0 or budget <= 0:
            break
        scale = budget / free
    scene = trimesh.Scene()
    for i, name in enumerate(groups):
        pieces = [result[k] for k, p in enumerate(parts) if p[0] == name]
        offsets = np.cumsum([0] + [len(p[0]) for p in pieces[:-1]])
        v, n, f = shade(np.concatenate([p[0] for p in pieces]),
                        np.concatenate([p[1] + o for p, o in zip(pieces, offsets)]))
        mesh = trimesh.Trimesh(v, f, vertex_normals=n, process=False)
        rgb = colors.get(name, (0.8, 0.8, 0.8))
        mesh.visual = trimesh.visual.TextureVisuals(material=trimesh.visual.material.PBRMaterial(
            name=name or "default", baseColorFactor=[*rgb, 1.0], metallicFactor=0.0, roughnessFactor=0.8))
        scene.add_geometry(mesh, geom_name=f"{i}_{name or 'default'}")
    with open(glb_path, "wb") as fp:
        fp.write(scene.export(file_type="glb"))
    return {"file": os.path.basename(obj_path), "tris_before": total,
            "tris_after": sum(len(g.faces) for g in scene.geometry.values()),
            "bytes_before": os.path.getsize(obj_path), "bytes_after": os.path.getsize(glb_path),
            "bbox_before": before, "bbox_after": scene.bounds,
            "bbox_err_mm": float(np.abs(scene.bounds - before).max() * 1000), "seconds": time.time() - t0}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--target-body", type=int, default=60000)
    ap.add_argument("--target-lidar", type=int, default=15000)
    ap.add_argument("--target-wheel", type=int, default=6000)
    ap.add_argument("--model-dir", default="dashboard/model")
    a = ap.parse_args()
    targets = {"body": a.target_body, "lidar_head": a.target_lidar,
               "wheel_left": a.target_wheel, "wheel_right": a.target_wheel}
    rows = []
    for name, target in targets.items():
        obj = os.path.join(a.model_dir, name + ".obj")
        if not os.path.exists(obj):
            print(f"{obj} 없음 — 건너뜀", file=sys.stderr)
            continue
        rows.append(decimate(obj, target, os.path.join(a.model_dir, name + ".glb")))
    print(f"{'파일':<16}{'삼각형 전':>10}{'삼각형 후':>10}{'크기 전 MB':>11}{'크기 후 MB':>11}"
          f"{'경계 상자(후, m)':>40}{'오차 mm':>9}{'시간 s':>8}")
    for r in rows:
        lo, hi = r["bbox_after"]
        box = "[" + " ".join(f"{c:.3f}" for c in lo) + "]~[" + " ".join(f"{c:.3f}" for c in hi) + "]"
        print(f"{r['file']:<16}{r['tris_before']:>10}{r['tris_after']:>10}{r['bytes_before'] / 1e6:>11.2f}"
              f"{r['bytes_after'] / 1e6:>11.2f} {box:>40}{r['bbox_err_mm']:>9.2f}{r['seconds']:>8.1f}")


if __name__ == "__main__":
    main()
