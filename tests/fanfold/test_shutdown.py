"""Exercise the real application and QML, on a private X server and bus only."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

binary, assets = map(str, map(Path, sys.argv[1:3]))
failures = []
for scenario in ("quit", "wayland", "wayland-failure", "pinned", "pinned-failure", "session", "session-failure", "state", "term", "term-failure", "normal-failure", "normal-absent-failure", "term-queued", "wayland-queued", "pinned-queued"):
    with tempfile.TemporaryDirectory(prefix="fanfold-shutdown-") as tmp:
        root = Path(tmp)
        for name in ("home", "notes", "state", "run"):
            (root / name).mkdir(mode=0o700)
        note = root / "notes/Fixture.md"
        note.write_text("original fixture\n")
        env = dict(os.environ, HOME=str(root / "home"),
                   XDG_CONFIG_HOME=str(root / "home/config"),
                   XDG_DATA_HOME=str(root / "home/data"),
                   XDG_CACHE_HOME=str(root / "home/cache"),
                   XDG_RUNTIME_DIR=str(root / "run"),
                   QT_QPA_PLATFORM="xcb", QT_QUICK_BACKEND="software",
                   FANFOLD_QML_ROOT=assets, FANFOLD_PROBE=scenario)
        for name in ("WAYLAND_DISPLAY", "SESSION_MANAGER", "DBUS_SESSION_BUS_ADDRESS"):
            env.pop(name, None)
        started = time.monotonic()
        process = subprocess.Popen(
            ["xvfb-run", "-a", "-s", "-screen 0 3440x1440x24", "dbus-run-session", "--",
             binary, "--root", str(root / "notes"), "--state", str(root / "state")],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            start_new_session=True)
        try:
            output = process.communicate(timeout=15)[0].decode(errors="replace")
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output = process.communicate()[0].decode(errors="replace")
        finally:
            # Private bus-activated services must not outlive the scenario.
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            (root / "notes").chmod(0o700)
        elapsed = time.monotonic() - started
        print(f"SCENARIO {scenario}: exit={process.returncode} elapsed={elapsed:.3f}s")
        print(output, end="")
        try:
            assert "PROBE ready" in output, "application did not reach real shell"
            assert process.returncode == 0, "process failed or quit was vetoed"
            if scenario.startswith("pinned"):
                index = json.loads((root / "notes/.fanfold/index.json").read_text())
                assert next(iter(index["documents"].values()))["pinned"], "shutdown unpinned the note"
            if scenario == "quit":
                assert note.read_text() == "original fixture\n"
            elif scenario.endswith("-failure"):
                assert note.read_text() == "original fixture\n"
                records = list((root / "notes/.fanfold/recovery").glob("*.json"))
                assert any(json.loads(p.read_text())["content"] == "pending shutdown fixture\n"
                           for p in records), "latest Markdown absent from recovery"
            elif scenario.endswith("-queued"):
                assert note.read_text() == "queued shutdown fixture\n", "queued Markdown not committed"
            else:
                assert note.read_text() == "pending shutdown fixture\n", "pending Markdown not committed"
        except AssertionError as error:
            failures.append(f"{scenario}: {error}")
        print()
if failures:
    sys.exit("\n".join(failures))
print("All shutdown scenarios passed")
