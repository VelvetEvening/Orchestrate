"""Verify a portable release without the SDK or personal application data."""

import argparse
import base64
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sqlite3
import subprocess
import tempfile
import time
import zipfile


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def quit_application(app):
    user32 = ctypes.windll.user32
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.PostThreadMessageW.argtypes = [wintypes.DWORD, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

    def quit_owned_window(hwnd, _):
        pid = wintypes.DWORD()
        thread = user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == app.pid:
            user32.PostThreadMessageW(thread, 0x0012, 0, 0)
        return True

    callback = callback_type(quit_owned_window)
    user32.EnumWindows(callback, 0)
    try:
        app.wait(timeout=5)
    except subprocess.TimeoutExpired:
        app.kill()
        app.wait()
        raise


def verify(version, schema, archive_path, source_commit, fixture_root):
    expected = archive_path.with_suffix(".zip.sha256").read_text().split()[0]
    assert re.fullmatch(r"[0-9a-fA-F]{64}", expected), "Invalid archive SHA256"
    assert sha256(archive_path) == expected.lower(), "Archive SHA256 mismatch"
    fixture_root.mkdir(parents=True, exist_ok=True)
    fixture = Path(tempfile.mkdtemp(prefix="发布验收 空格-", dir=fixture_root))
    package_name = f"Orchestrate-v{version}-windows-x64"
    with zipfile.ZipFile(archive_path) as archive:
        for name in archive.namelist():
            path = PurePosixPath(name)
            assert not path.is_absolute() and ".." not in path.parts and "\\" not in name, name
            assert path.parts and path.parts[0] == package_name, name
            assert not any(part in {"data", "state", "tests", ".git", "CMakeFiles"} for part in path.parts), name
            assert not name.endswith((".log", ".pdb", "CMakeCache.txt")), name
        archive.extractall(fixture)

    package = fixture / package_name
    verified = set()
    for line in (package / "files.sha256").read_text().splitlines():
        digest, name = line.split("  ", 1)
        relative = PurePosixPath(name)
        assert not relative.is_absolute() and ".." not in relative.parts and "\\" not in name, name
        assert name not in verified, f"Duplicate manifest entry: {name}"
        assert sha256(package / name) == digest, name
        verified.add(name)
    payload = {path.relative_to(package).as_posix() for path in package.rglob("*") if path.is_file()}
    assert payload == verified | {"files.sha256"}, "Manifest does not cover the complete package"
    build_info = json.loads((package / "build-info.json").read_text())
    assert build_info["version"] == version
    assert build_info["release_tag"] == f"v{version}"
    assert build_info["source_commit"] == source_commit
    for relative in (
        "Orchestrate.exe", "OrchestrateMath.exe", "Qt6Core.dll", "Qt6Gui.dll",
        "Qt6Widgets.dll", "Qt6Sql.dll", "Qt6Network.dll", "Qt6Svg.dll",
        "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll",
        "iconengines/qsvgicon.dll", "platforms/qwindows.dll", "sqldrivers/qsqlite.dll",
        "tls/qschannelbackend.dll", "updater/Update-Orchestrate.ps1",
        "tools/scheduled-shutdown/orchestrate-tool.json",
    ):
        assert (package / relative).is_file(), relative

    environment = dict(os.environ)
    environment["PATH"] = str(Path(os.environ["WINDIR"]) / "System32")
    for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM", "QML2_IMPORT_PATH"):
        environment.pop(key, None)
    codes = [
        r"o_t=\operatorname{softmax}\left(\frac{q_tK_{1:t}^{\mathsf T}}{\sqrt{d_k}}\right)V_{1:t}",
        r"\underbrace{x}_{\text{中文注释}}", r"\unsupported{x}",
    ]
    render = subprocess.run(
        [str(package / "OrchestrateMath.exe")],
        input=json.dumps({"size": 20, "formulas": [{"code": code, "display": True} for code in codes]}, ensure_ascii=False).encode(),
        capture_output=True, timeout=7, cwd=fixture, env=environment,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    assert render.returncode == 0, render.stderr
    results = [json.loads(line) for line in render.stdout.splitlines()]
    assert len(results) == 3
    for result in results[:2]:
        assert base64.b64decode(result["png"]).startswith(b"\x89PNG\r\n\x1a\n")
    assert results[2]["error"] is True

    data = package / "data"
    data.mkdir()
    with sqlite3.connect(data / "orchestrate.sqlite3") as database:
        database.execute("CREATE TABLE app_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)")
        database.executemany("INSERT INTO app_settings VALUES (?, 'false')", [
            ("global_shortcut_enabled",), ("clipboard.monitor",),
        ])
    (data / "update-check.json").write_text(json.dumps({"automatic_enabled": False}), encoding="utf-8")
    health_dir = fixture / ".Orchestrate-update-package-verification"
    health_dir.mkdir()
    health = health_dir / "startup.json"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    app = subprocess.Popen(
        [str(package / "Orchestrate.exe"), "--update-health-file", str(health)],
        cwd=fixture, env=environment, startupinfo=startup,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not health.exists() and app.poll() is None:
            time.sleep(.05)
        assert health.exists(), f"Startup did not complete: {app.poll()}"
        status = json.loads(health.read_text())
        assert status["version"] == version and status["process_id"] == app.pid and status["window_visible"]
    finally:
        quit_application(app)
    assert app.returncode == 0
    with sqlite3.connect(data / "orchestrate.sqlite3") as database:
        assert database.execute("PRAGMA user_version").fetchone()[0] == schema
        assert database.execute("PRAGMA quick_check").fetchone()[0] == "ok"
        if schema >= 8:
            columns = {row[1] for row in database.execute("PRAGMA table_info(projects)")}
            assert "outline" not in columns
            assert {"target_type", "target_name", "target_user"} <= columns
    return {
        "source_commit": build_info["source_commit"], "schema": schema, "sha256": expected,
        "verified_files": len(verified), "worker_formulas": 2, "unsupported_fallback": True,
        "native_startup": status, "clean_exit": True, "sdk_paths_removed": True, "fixture": str(fixture),
    }


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="Application version, e.g. 3.7.0 or v3.7.0")
    parser.add_argument("schema", type=int, help="Expected SQLite schema version")
    parser.add_argument("--archive", type=Path, help="Archive path (defaults to dist/)")
    parser.add_argument("--source-commit", help="Expected source commit (defaults to HEAD)")
    parser.add_argument("--fixture-root", type=Path, default=root / "app/build-qt")
    args = parser.parse_args()
    version = args.version.removeprefix("v")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version) or args.schema < 1:
        parser.error("Use a three-part version and a positive schema version")
    source_commit = args.source_commit or subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True,
    ).strip()
    archive_path = (args.archive or root / f"dist/Orchestrate-v{version}-windows-x64.zip").resolve()
    print(json.dumps(verify(version, args.schema, archive_path, source_commit, args.fixture_root.resolve()), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
