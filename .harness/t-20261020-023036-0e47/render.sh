#!/bin/sh
# 확인용: SVG 를 headless Chrome 으로 PNG 렌더링 (프로필·출력은 E: 에)
# 사용: render.sh <name> <width> <height> [virtual-ms]
here="$(cd "$(dirname "$0")" && pwd -W)"
root="$(cd "$(dirname "$0")/../.." && pwd -W)"
mkdir -p "$(dirname "$0")/out" "$(dirname "$0")/profile"
"/c/Program Files/Google/Chrome/Application/chrome.exe" --headless=new --disable-gpu --hide-scrollbars \
  --user-data-dir="$here/profile" --window-size="$2,$3" --virtual-time-budget="${4:-2500}" \
  --screenshot="$here/out/$1-${4:-2500}.png" "file:///$root/docs/assets/$1.svg" 2>/dev/null
echo "$here/out/$1-${4:-2500}.png"
