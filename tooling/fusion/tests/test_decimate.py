"""tooling/fusion/decimate_model.py 시험: 재질 2개 상자 둘 합성 메시 + (있으면) 실제 대시보드 모델."""
import hashlib
import os
import sys

import numpy as np
import trimesh

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import decimate_model  # noqa: E402

MODEL_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "..", "dashboard", "model")
COLORS = {"red": (1.0, 0.0, 0.0), "blue": (0.0, 0.0, 1.0)}


def write_boxes(tmp_path):
    """잘게 나눈(4^3 배) 상자 둘을 재질 red/blue 로 쓴 OBJ+MTL 경로와 삼각형 수."""
    lines, base, total = ["mtllib boxes.mtl"], 0, 0
    for name, offset in (("red", (0, 0, 0)), ("blue", (0.3, 0, 0))):
        box = trimesh.creation.box(extents=(0.2, 0.1, 0.05)).subdivide().subdivide().subdivide()
        box.apply_translation(offset)
        lines.append(f"g {name}\nusemtl {name}")
        lines += [f"v {x:.6f} {y:.6f} {z:.6f}" for x, y, z in box.vertices]
        lines += [f"f {a + base + 1} {b + base + 1} {c + base + 1}" for a, b, c in box.faces]
        base += len(box.vertices)
        total += len(box.faces)
    obj = tmp_path / "boxes.obj"
    obj.write_text("\n".join(lines) + "\n", encoding="utf-8")
    (tmp_path / "boxes.mtl").write_text(
        "".join(f"newmtl {n}\nKd {r} {g} {b}\n\n" for n, (r, g, b) in COLORS.items()), encoding="utf-8")
    return obj, total


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_synthetic_boxes(tmp_path):
    obj, total = write_boxes(tmp_path)
    before = digest(obj), digest(tmp_path / "boxes.mtl")
    target = 300
    r = decimate_model.decimate(str(obj), target, str(tmp_path / "boxes.glb"))
    assert r["tris_before"] == total
    # (1) 목표 삼각형 수 ±20%
    assert abs(r["tris_after"] - target) <= 0.2 * target, r["tris_after"]
    # (2) 경계 상자 오차 1 mm 이하
    assert r["bbox_err_mm"] <= 1.0
    # (3) glb 를 다시 읽으면 재질 2개 · 색 유지, 면은 상자 바깥을 향한다
    scene = trimesh.load(str(tmp_path / "boxes.glb"))
    got = {g.visual.material.name: g for g in scene.geometry.values()}
    assert set(got) == set(COLORS)
    for name, rgb in COLORS.items():
        color = np.asarray(got[name].visual.material.baseColorFactor, dtype=float)[:3] / 255.0
        assert np.allclose(color, rgb, atol=0.01)
        mesh = got[name]
        outward = (mesh.face_normals * (mesh.triangles_center - mesh.bounds.mean(0))).sum(1)
        assert (outward > 0).all(), "뒤집힌 면"
    # (4) 원본 obj/mtl 불변
    assert (digest(obj), digest(tmp_path / "boxes.mtl")) == before


def test_real_body_model(tmp_path):
    obj = os.path.join(MODEL_DIR, "body.obj")
    if not os.path.exists(obj):
        print(f"{obj} 없음(모델은 git 에 없다) — 실제 모델 시험은 건너뛰고 통과로 둔다")
        return
    target = 60000
    r = decimate_model.decimate(obj, target, str(tmp_path / "body.glb"))
    print(r)
    assert r["tris_before"] > 500000
    assert abs(r["tris_after"] - target) <= 0.2 * target, r["tris_after"]
    assert r["bbox_err_mm"] <= 1.0
