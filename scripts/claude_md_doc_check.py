#!/usr/bin/env python3
"""PostToolUse hook: when CLAUDE.md is edited, inject a reminder for Claude to self-review the
change against the "is this derivable from the code?" test before moving on. Reads the tool
input JSON from stdin; only fires for CLAUDE.md."""
import json
import sys

try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)

path = data.get("tool_input", {}).get("file_path", "")
if not path.endswith("CLAUDE.md"):
    sys.exit(0)

msg = (
    "You just edited CLAUDE.md. Re-read each changed line and apply the test: \"could a "
    "competent engineer derive this from the code in a couple of minutes?\" If yes, it does "
    "not belong. Strip exact signatures, parameter/field/variable names, route names, payload "
    "byte layouts, and menu/route enumerations; keep only the load-bearing mental model, "
    "non-obvious invariants/gotchas, the \"why\", and pointers to where the truth lives in "
    "source. The result should usually be shorter."
)
print(json.dumps({"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": msg}}))
