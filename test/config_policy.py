"""Static checks on growell-display.yaml that the C++ tests can't see.

- No open fallback hotspot: `captive_portal` pulls in an unauthenticated firmware
  upload page, and the board carries a Home Assistant token.
- `min_version` must be the ESPHome release the file is actually built with
  (the image tag in tools/esphome.ps1), not an older one we never compiled.
- The GT911 must not declare `interrupt_pin`: the 8048S070C leaves INT unconnected
  and ESPHome stops polling once an interrupt pin is set, so touch would be dead.

Usage: python3 test/config_policy.py [path/to/config.yaml]
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
config = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "growell-display.yaml"
yaml_text = config.read_text(encoding="utf-8-sig")
runner = (root / "tools" / "esphome.ps1").read_text(encoding="utf-8")
problems = []

if re.search(r"^captive_portal:", yaml_text, re.M):
    problems.append("captive_portal is enabled (unauthenticated firmware upload on the fallback hotspot)")
if re.search(r"^\s+ap:", yaml_text, re.M):
    problems.append("wifi fallback hotspot (ap:) is enabled")

image = re.search(r"ghcr\.io/esphome/esphome:([\d.]+)", runner)
min_version = re.search(r"^\s+min_version:\s*([\d.]+)", yaml_text, re.M)
if not image or not min_version:
    problems.append("could not find the ESPHome image tag or min_version")
elif image.group(1) != min_version.group(1):
    problems.append(f"min_version {min_version.group(1)} != built-with ESPHome {image.group(1)}")

touch = re.search(r"^touchscreen:\n((?:[ \t].*\n?|\n)*)", yaml_text, re.M)
if touch and re.search(r"^\s+interrupt_pin:", touch.group(1), re.M):
    problems.append("gt911 declares interrupt_pin (INT is not connected on the 8048S070C; touch would never fire)")

for p in problems:
    print("POLICY FAIL:", p)
print(f"config policy: {len(problems)} problem(s)")
sys.exit(1 if problems else 0)
