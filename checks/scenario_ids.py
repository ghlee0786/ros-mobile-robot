"""Print the offline scenario ids for merge checks, comma-separated: everything except the long soak run.

checks/integration.cmd uses this unless FULL=1 (then every scenario runs, soak included).
"""
import glob
import json
import os

LONG = {"soak"}
here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tooling", "mockbridge", "scenarios")
ids = []
for f in sorted(glob.glob(os.path.join(here, "*.json"))):
    s = json.load(open(f, encoding="utf-8"))
    if s["id"] not in LONG and not s.get("needs_pi"):
        ids.append(s["id"])
print(",".join(ids))
