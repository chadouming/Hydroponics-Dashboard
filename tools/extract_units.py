"""Copy every '// BEGIN unit <name>' ... '// END unit <name>' block out of
growell-display.yaml into test/build/<name>.inc so test/logic_test.cpp can
compile exactly the code that ships in the YAML."""
import pathlib
import re
import textwrap

root = pathlib.Path(__file__).resolve().parent.parent
source = (root / "growell-display.yaml").read_text(encoding="utf-8")
out = root / "test" / "build"
out.mkdir(parents=True, exist_ok=True)
for stale in out.glob("*.inc"):
    stale.unlink()

pattern = re.compile(r"^[ \t]*// BEGIN unit (\w+)[^\n]*\n(.*?)^[ \t]*// END unit \1\b", re.S | re.M)
names = []
for match in pattern.finditer(source):
    name, body = match.group(1), textwrap.dedent(match.group(2))
    (out / f"{name}.inc").write_text(body, encoding="utf-8")
    names.append(name)
print("extracted units:", ", ".join(sorted(names)) or "(none)")
