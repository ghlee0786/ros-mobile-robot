"""docs/assets/*.svg 검사: XML 형식, 금지 요소(script·foreignObject·외부 href), viewBox, 300 KB 이하.

하나라도 어기면 종료 코드 1. 표준 라이브러리만 쓴다.
"""
import pathlib
import sys
import xml.etree.ElementTree as ET

MAX_BYTES = 300 * 1024
HERE = pathlib.Path(__file__).resolve().parent


def local(tag):
    return tag.rsplit("}", 1)[-1]


def check(path):
    errors = []
    if path.stat().st_size > MAX_BYTES:
        errors.append(f"크기 {path.stat().st_size} B > {MAX_BYTES} B")
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as e:
        return errors + [f"XML 오류: {e}"]
    if local(root.tag) != "svg":
        errors.append("루트가 <svg> 가 아님")
    if not root.get("viewBox"):
        errors.append("viewBox 없음")
    for el in root.iter():
        name = local(el.tag)
        if name in ("script", "foreignObject"):
            errors.append(f"금지 요소 <{name}>")
        for key, value in el.attrib.items():
            if local(key) == "href" and value.strip().lower().startswith(("http:", "https:", "//")):
                errors.append(f"외부 href: {value}")
        if name == "style" and el.text and ("@import" in el.text or "url(http" in el.text):
            errors.append("<style> 안 외부 참조")
    return errors


def main():
    sys.stdout.reconfigure(errors="replace")  # 콘솔 코드 페이지가 한글을 못 찍어도 죽지 않게
    files = sorted(HERE.glob("*.svg"))
    if not files:
        print("SVG 파일이 없음")
        return 1
    failed = False
    for path in files:
        errors = check(path)
        print(f"{'FAIL' if errors else 'ok  '} {path.name}")
        for e in errors:
            print(f"     - {e}")
        failed = failed or bool(errors)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
