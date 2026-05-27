#!/bin/bash
# PostToolUse hook: auto-format C++ files after Claude edits them.
# Reads the tool input JSON from stdin and extracts file_path.
CF="${CLANG_FORMAT:-/Users/bart/git/espressif/llvm-project/build/bin/clang-format}"
file=$(python3 -c "import json,sys; d=json.load(sys.stdin); print(d.get('tool_input',{}).get('file_path',''))" 2>/dev/null)
[[ "$file" =~ \.(cpp|h|hpp|c)$ ]] && [[ -x "$CF" ]] && "$CF" -i "$file"
