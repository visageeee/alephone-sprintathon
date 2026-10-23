#!/usr/bin/env python3
"""Stamp the checked-out commit and optional release tag before compilation."""
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
if not re.fullmatch(r"[0-9a-f]{40,64}", commit):
    raise SystemExit("Invalid Git commit identity")
tag = os.environ.get("SPRINTATHON_RELEASE_TAG", "")
if tag and (len(tag) > 128 or any(ord(c) < 33 or ord(c) > 126 for c in tag)):
    raise SystemExit("Release tag must contain 1-128 printable ASCII characters without spaces")
path = root / "Source_Files/Misc/SprintathonBuildVersion.h"
text = ("#ifndef SPRINTATHON_BUILD_VERSION_H\n#define SPRINTATHON_BUILD_VERSION_H\n"
        "// Generated build identity; do not hand-edit.\n"
        f"#define SPRINTATHON_BUILD_TAG {json.dumps(tag)}\n"
        f"#define SPRINTATHON_BUILD_COMMIT {json.dumps(commit)}\n#endif\n")
if not path.exists() or path.read_text() != text:
    path.write_text(text)
print(f"Sprintathon build: {tag or 'development'} ({commit[:12]})")
