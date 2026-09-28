#!/usr/bin/env python3
"""Dev helper: re-joins C string literals whose \\n escape was turned into a real
line break (the next line then starts with the closing quote)."""
import re
import sys

for path in sys.argv[1:]:
    s = open(path).read()
    fixed = re.sub(r'\n(?="[,);])', lambda m: "\\n", s)
    if fixed != s:
        open(path, "w").write(fixed)
    print(path, s.count("\n") - fixed.count("\n"), "literals repaired")
