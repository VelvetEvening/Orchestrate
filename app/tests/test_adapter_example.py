"""Run only the harmless documentation adapter in a temporary copy."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from unittest.mock import patch


source = Path(__file__).resolve().parents[2] / "docs/examples/tool-adapter/adapter.py"
with tempfile.TemporaryDirectory(prefix="orchestrate-example-") as temporary:
    root = Path(temporary) / "中文 tool with spaces"
    root.mkdir()
    script = root / "adapter.py"
    shutil.copy2(source, script)
    state_path = root / "state/current.json"

    def run(*args):
        return subprocess.run([sys.executable, str(script), *args], cwd=temporary,
                              capture_output=True, encoding="utf-8", timeout=10)

    completed = run("run", "--message", "中文 {other} ' spaces", "--count", "3")
    assert completed.returncode == 0, completed.stderr
    state = json.loads(state_path.read_text(encoding="utf-8"))
    assert state["schema"] == "orchestrate-state/v1" and state["tool_id"] == "example-adapter"
    assert state["result"] == "success" and len(state["items"]) == 3
    assert state["items"][0]["summary"] == "中文 {other} ' spaces"
    from datetime import datetime, date
    assert datetime.fromisoformat(state["updated_at"]).tzinfo is not None
    assert date.fromisoformat(state["current_date"])
    for args in [("fail",), ("run", "--count", "99")]:
        failed = run(*args)
        assert failed.returncode == 1 and failed.stderr
        assert json.loads(state_path.read_text(encoding="utf-8"))["result"] == "failure"
    assert run("run", "--uppercase", "--message", "hello").returncode == 0
    before = state_path.read_bytes()
    spec = importlib.util.spec_from_file_location("adapter_example", script)
    adapter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(adapter)
    with patch.object(adapter.os, "replace", side_effect=OSError("simulated replacement failure")):
        try:
            adapter.atomic_json(state_path, {"result": "failure"})
            raise AssertionError("Expected replace failure")
        except OSError:
            pass
    assert state_path.read_bytes() == before
    assert not list((root / "state").glob("*.tmp"))
print("PASS adapter success/failure, argv, independent cwd, timestamps, atomic replacement failure")
