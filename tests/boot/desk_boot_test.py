#!/usr/bin/env python3
"""Exercise boot selection and input discovery with isolated filesystem/command fakes."""
import os
import pathlib
import subprocess
import tempfile


def main():
    repo = pathlib.Path(__file__).resolve().parents[2]
    app = (repo / "br2-external/board/taq102/rootfs-overlay/usr/bin/taq102-app").read_text()
    launcher = (repo / "br2-external/package/dmxdesk/taq102-desk").read_text()
    cases = [
        ("desk then cube", True, True, "", False, False, ["desk"] * 5 + ["cube"] * 5),
        ("vendor cube", False, False, "", False, False, ["cube"] * 5),
        ("non-executable desk", True, False, "", False, False, ["cube"] * 5),
        ("explicit override", True, True, "custom", False, False, ["custom"] * 5),
        ("rescue marker", True, True, "", True, False, ["rescue"]),
        ("held button", True, True, "", False, True, ["rescue"]),
    ]
    for name, desk, executable, override, marker, held, expected in cases:
        with tempfile.TemporaryDirectory(prefix="desk-boot-", dir=repo / "output") as work:
            root = pathlib.Path(work)
            for directory in ("usr/bin", "etc", "dev/dri", "data/log", "sys/class/vtconsole/vtcon0", "mocks"):
                (root / directory).mkdir(parents=True, exist_ok=True)
            (root / "dev/dri/card0").touch()
            (root / "sys/class/vtconsole/vtcon0/name").write_text("frame buffer\n")
            trace = root / "trace"
            for command, label in (("glcube", "cube"), ("taq102-desk", "desk"), ("custom", "custom"), ("rescue-screen", "rescue")):
                if command == "taq102-desk" and not desk:
                    continue
                path = root / "usr/bin" / command
                path.write_text(f'#!/bin/sh\nprintf "%s\\n" {label} >> "$BOOT_TRACE"\nexit 1\n')
                path.chmod(0o644 if command == "taq102-desk" and not executable else 0o755)
            for command in ("evtest", "sleep", "usleep"):
                path = root / "mocks" / command
                path.write_text(f"#!/bin/sh\nexit {10 if command == 'evtest' and held else 0}\n")
                path.chmod(0o755)
            if marker:
                (root / "etc/taq102-no-autostart").touch()
            script = app
            for prefix in ("/usr/", "/etc/", "/dev/", "/data/", "/sys/", "/tmp/"):
                script = script.replace(prefix, str(root) + prefix)
            env = dict(os.environ, BOOT_TRACE=str(trace), PATH=str(root / "mocks") + os.pathsep + os.environ["PATH"],
                       TAQ102_APP=str(root / "usr/bin" / override) if override else "")
            result = subprocess.run(["sh"], input=script, text=True, env=env, capture_output=True, timeout=5)
            assert result.returncode == (1 if marker or held else 0), (name, result.stderr)
            assert trace.read_text().splitlines() == expected, name
            print("PASS", name)
    for touch, power, present in ((7, 0, True), (0, 7, True), (7, 0, False)):
        with tempfile.TemporaryDirectory(prefix="desk-input-", dir=repo / "output") as work:
            root = pathlib.Path(work)
            for directory in ("usr/bin", "dev/input", "mocks"):
                (root / directory).mkdir(parents=True, exist_ok=True)
            for event, name in ((touch, "silead_ts"), (power, "rk805 pwrkey"), (9, "unrelated device")):
                path = root / f"sys/class/input/event{event}/device"
                path.mkdir(parents=True)
                (path / "name").write_text(name + "\n" if present else "unrelated\n")
                (root / f"dev/input/event{event}").touch()
            trace = root / "trace"
            path = root / "usr/bin/dmxdesk"
            path.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$BOOT_TRACE"\n')
            path.chmod(0o755)
            path = root / "mocks/sleep"
            path.write_text("#!/bin/sh\nexit 0\n")
            path.chmod(0o755)
            script = launcher
            for prefix in ("/usr/", "/dev/", "/sys/"):
                script = script.replace(prefix, str(root) + prefix)
            env = dict(os.environ, BOOT_TRACE=str(trace), PATH=str(root / "mocks") + os.pathsep + os.environ["PATH"])
            result = subprocess.run(["sh"], input=script, text=True, env=env, capture_output=True, timeout=5)
            if present:
                assert result.returncode == 0, result.stderr
                assert trace.read_text().splitlines() == ["--map", str(root / "usr/share/dmxdesk/vibra.desk.json"),
                    "--touch", str(root / f"dev/input/event{touch}"), "--power", str(root / f"dev/input/event{power}")]
            else:
                assert result.returncode == 1 and "missing silead_ts" in result.stderr
                assert not trace.exists()
            print(f"PASS input discovery touch={touch} power={power} present={present}")
    print("9 boot/launcher scenarios passed")


if __name__ == "__main__":
    main()
