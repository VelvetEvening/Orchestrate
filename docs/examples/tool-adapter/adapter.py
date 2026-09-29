"""Small, local-only adapter example. Python 3.9+, standard library only."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parent
STATE = ROOT / "state" / "current.json"
TOOL_ID = "example-adapter"


def atomic_json(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        # Same directory/filesystem; close the file before replacing on Windows.
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=".current-", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(payload, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def publish(result, summary, command, settings, items=None):
    now = datetime.now().astimezone()
    atomic_json(STATE, {
        "schema": "orchestrate-state/v1",
        "tool_id": TOOL_ID,
        "current_date": now.date().isoformat(),
        "updated_at": now.isoformat(timespec="seconds"),
        "result": result,
        "summary": summary,
        "last_command": command,
        "items": items or [],
        "settings": settings,
    })


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["run", "fail"])
    parser.add_argument("--message", default="你好 Orchestrate")
    parser.add_argument("--count", type=int, default=2)
    parser.add_argument("--uppercase", action="store_true")
    args = parser.parse_args(argv)
    settings = {"message": args.message, "count": args.count, "uppercase": args.uppercase}
    try:
        # Business validation is also required for CLI/scheduler calls outside the UI.
        if not args.message.strip() or not 1 <= args.count <= 5:
            raise ValueError("message must be non-empty and count must be between 1 and 5")
        publish("running", "正在生成示例结果", args.action, settings)
        if args.action == "fail":
            raise RuntimeError("用于验收的模拟失败，没有执行外部操作")
        message = args.message.upper() if args.uppercase else args.message
        items = [{"name": f"item-{index + 1}", "result": "success", "summary": message}
                 for index in range(args.count)]
        publish("success", f"已生成 {len(items)} 条示例结果", args.action, settings, items)
        print(f"Completed: {len(items)} items", flush=True)
        return 0
    except Exception as error:
        # A failure must replace the previous success snapshot, not merely print stderr.
        try:
            publish("failure", str(error), args.action, settings)
        except OSError as state_error:
            print(f"Cannot write failure state: {state_error}", file=sys.stderr, flush=True)
        print(str(error), file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
