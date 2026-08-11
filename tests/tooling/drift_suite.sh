#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$ROOT/tests/tooling/drift_check.py"
python3 "$ROOT/scripts/integration-patch-id.py" --self-test
