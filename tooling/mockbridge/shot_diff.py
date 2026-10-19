"""두 스크린샷 PNG 의 픽셀 차이 비율을 낸다(Pillow). 판정이 아니라 참고 경고용이다.

    uv run --no-project --with pillow python tooling/mockbridge/shot_diff.py 기준.png 새것.png [--threshold 24] [--out diff.png]
크기가 다르면 새것을 기준 크기로 줄여 비교한다. 출력: 'diff=<비율>' (0~1). 종료 코드는 늘 0.
"""
import argparse
import sys

from PIL import Image, ImageChops


def diff_ratio(base, new, threshold=24, out=None):
    """채널 최대 차이가 threshold 를 넘는 픽셀의 비율."""
    a = Image.open(base).convert("RGB")
    b = Image.open(new).convert("RGB")
    if b.size != a.size:
        b = b.resize(a.size)
    d = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > threshold else 0)
    if out:
        d.save(out)
    return d.histogram()[255] / float(a.size[0] * a.size[1])


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("base")
    p.add_argument("new")
    p.add_argument("--threshold", type=int, default=24, help="픽셀이 다르다고 볼 밝기 차이(0~255)")
    p.add_argument("--out", help="차이 마스크 PNG")
    a = p.parse_args(argv)
    print(f"diff={diff_ratio(a.base, a.new, a.threshold, a.out):.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
