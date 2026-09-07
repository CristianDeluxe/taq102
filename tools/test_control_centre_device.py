"""Exercise the live control centre and always return ownership to the supervisor."""
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shlex
import signal
import subprocess
import sys
import time

import numpy as np


class DeviceTest:
    def __init__(self):
        self.out = Path(sys.argv[1] if len(sys.argv) > 1 else
                        '/tmp/taq102-audit/device/' + time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()))
        self.out.mkdir(parents=True, exist_ok=False)
        self.log = (self.out / 'output.log').open('w')
        self.ssh = ['ssh', '-i', str(Path.home() / '.ssh/taq102'), '-o', 'BatchMode=yes',
                    '-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
                    '-o', 'LogLevel=ERROR', '-o', 'ConnectTimeout=8',
                    '-o', 'ServerAliveInterval=5', '-o', 'ServerAliveCountMax=3',
                    'root@' + os.environ.get('TAQ102_HOST', 'taq102.local')]
        self.sim = None
        self.restore_needed = False
        self.failures = 0
        self.number = 0
        self.flipped = False
        self.original = None
        self.original_brightness = 255
        self.test_settings = None
        self.remote = ''
        self.last_line = ''
        self.seconds = int(os.environ.get('TAQ102_TEST_SECONDS', '60'))
        if not 3 <= self.seconds <= 600:
            raise ValueError('TAQ102_TEST_SECONDS must be 3..600 (acceptance: 60)')

    def check(self, label, ok, numbers):
        self.number += 1
        line = f'{"PASS" if ok else "FAIL"} {self.number:02d} {label}: {numbers}'
        print(line, flush=True)
        self.log.write(line + '\n'); self.log.flush()
        if not ok:
            self.failures += 1
            raise RuntimeError(label)

    def ssh_run(self, command, data=None, timeout=25):
        result = subprocess.run(self.ssh + [command], input=data, capture_output=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError(f'SSH exit {result.returncode}: {command}\n{result.stderr.decode(errors="replace")}')
        return result.stdout

    def text(self, command):
        return self.ssh_run(command).decode(errors="replace")

    def optional_file(self, path):
        path = shlex.quote(path)
        data = self.ssh_run(f'if test -e {path}; then printf 1; cat {path}; else printf 0; fi')
        return data[1:] if data.startswith(b'1') else None

    def trace(self):
        data = self.text('cat /tmp/glcube-test.log')
        (self.out / 'glcube-test.log').write_text(data)
        return data

    def wait_for(self, predicate, label, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.15)
        raise RuntimeError('timeout: ' + label)

    def ack(self, expected, timeout=15):
        assert self.sim is not None and self.sim.stdout is not None
        ready, _, _ = select.select([self.sim.stdout], [], [], timeout)
        line = self.sim.stdout.readline().decode().strip() if ready else ''
        self.last_line = line
        if line != expected:
            raise RuntimeError(f'touchsim: expected {expected!r}, received {line!r}')

    def send(self, command):
        assert self.sim is not None and self.sim.stdin is not None
        with (self.out / 'commands.log').open('a') as log:
            log.write(command + '\n')
        self.sim.stdin.write((command + '\n').encode()); self.sim.stdin.flush()

    def command(self, command):
        self.send(command)
        self.ack('OK ' + command.split()[0], max(15, int(command.split()[-1]) / 1000 + 5)
                 if command.startswith(('drag ', 'hold ', 'pinch ', 'sleep ')) else 15)

    def point(self, x, y):
        return (1024 - x, 600 - y) if self.flipped else (x, y)

    def tap(self, x, y):
        x, y = self.point(x, y); self.command(f'tap {x} {y}')

    def drag(self, x1, y1, x2, y2, ms=500, asynchronous=False):
        a, b = self.point(x1, y1), self.point(x2, y2)
        command = f'drag {a[0]} {a[1]} {b[0]} {b[1]} {ms}'
        (self.send if asynchronous else self.command)(command)

    def panel(self, opened):
        if opened:
            self.drag(900, 10, 900, 200, 300)
        else:
            self.tap(100, 500)
        self.command('sleep 250')

    def brightness(self):
        return int(self.text('cat /sys/class/backlight/backlight/brightness'))

    def config(self):
        text = self.text('cat /tmp/taq102-test.conf')
        (self.out / 'test-settings.conf').write_text(text)
        return dict(line.split('=', 1) for line in text.splitlines())

    def capture(self, name):
        summary = self.text('cat /sys/kernel/debug/dri/0/summary')
        (self.out / (name + '-summary.log')).write_text(summary)
        if not all(part in summary for part in ('XR24', '1024x600', 'pitch: 4096')):
            raise RuntimeError('unexpected scanout format')
        address = int(re.search(r'addr: (0x[0-9a-fA-F]+)', summary)[1], 16)
        if address % 4096:
            raise RuntimeError('unaligned scanout address')
        # /dev/mem is only ever an input. No device or block writes occur here.
        raw = self.ssh_run(f'dd if=/dev/mem bs=4096 skip={address // 4096} count=600 2>{self.remote}/scanout-dd.log')
        (self.out / (name + '.raw')).write_bytes(raw)
        if len(raw) != 1024 * 600 * 4:
            raise RuntimeError(f'short scanout: {len(raw)} bytes')
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'bgra',
                        '-s', '1024x600', '-i', str(self.out / (name + '.raw')),
                        '-frames:v', '1', str(self.out / (name + '.png'))], check=True)
        pixels = np.frombuffer(raw, dtype=np.uint8).reshape(600, 1024, 4)[:, :, :3]
        if self.flipped:
            pixels = pixels[::-1, ::-1]
        # Glass at the right card margin, away from tiles and the moving cube.
        glass = pixels[100:380, 964:972].astype(int)
        count = int(np.all(np.abs(glass - np.array([26, 19, 18])) <= 6, axis=2).sum())
        amber = pixels[258:314, 860:948].astype(int)
        amber_count = int(((amber[:, :, 2] > 170) & (amber[:, :, 1] > 80) & (amber[:, :, 0] < 35)).sum())
        info = {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
                'glass_pixels': count, 'rescue_amber_pixels': amber_count, 'flipped': self.flipped}
        (self.out / (name + '-counts.json')).write_text(json.dumps(info, indent=2) + '\n')
        return info

    def performance(self, name, action=None):
        start_lines = len(self.trace().splitlines())
        start = time.monotonic()
        while time.monotonic() - start < self.seconds:
            if action:
                action()
            else:
                time.sleep(1)
        lines = self.trace().splitlines()[start_lines:]
        (self.out / (name + '-performance.log')).write_text('\n'.join(lines) + '\n')
        frames = [float(re.match(r'([\d.]+) FPS', line)[1]) for line in lines if re.match(r'[\d.]+ FPS', line)]
        uploads = [float(re.search(r'uploads/s ([\d.]+)', line)[1]) for line in lines if 'uploads/s' in line]
        errors = re.findall(r'glGetError (0x[0-9a-f]+)', '\n'.join(lines))
        self.check(name + f' {self.seconds}-second FPS/GL', len(frames) >= self.seconds - 2 and all(f == 54.8 for f in frames)
                   and len(errors) == len(frames) and all(e == '0x0' for e in errors),
                   f'seconds={time.monotonic()-start:.2f}, samples={len(frames)}, '
                   f'FPS={min(frames, default=0)}..{max(frames, default=0)}, '
                   f'uploads/s={min(uploads, default=0)}..{max(uploads, default=0)}, GL_errors={sum(e != "0x0" for e in errors)}')
        if name == 'closed':
            self.check('hidden panel uploads', all(u == 0 for u in uploads), f'samples={len(uploads)}, uploads={sum(uploads)}')
        if name == 'dragging':
            self.check('drag upload rate', uploads and 0 < max(uploads) <= 31,
                       f'max_uploads/s={max(uploads, default=0)}')

    def stop_apps(self):
        # Only the named application processes; wait for DRM ownership to end.
        self.ssh_run('killall taq102-app 2>/dev/null || :; '
                     'killall glcube 2>/dev/null || :; '
                     'i=0; while pidof glcube >/dev/null || pidof taq102-app >/dev/null; do '
                     'i=$((i+1)); test "$i" -lt 100 || exit 1; usleep 100000; done; usleep 300000')

    def prepare(self):
        subprocess.run(['ffmpeg', '-version'], stdout=subprocess.DEVNULL, check=True)
        self.ssh_run('test -x /usr/bin/touchsim && test -c /dev/uinput && test -x /usr/bin/glcube && '
                     'test -f /usr/share/fonts/taq102/Inter-Regular.ttf && '
                     'test -f /usr/share/fonts/taq102/Inter-SemiBold.ttf && '
                     'test ! -f /etc/taq102-no-autostart && ! pidof touchsim >/dev/null')
        self.original = self.optional_file('/data/taq102.conf')
        (self.out / 'original-settings.conf').write_bytes(self.original if self.original is not None else b'MISSING')
        self.test_settings = self.optional_file('/tmp/taq102-test.conf')
        (self.out / 'previous-test-settings.conf').write_bytes(self.test_settings if self.test_settings is not None else b'MISSING')
        self.original_brightness = self.brightness()
        self.remote = self.text('mktemp -d /tmp/taq102-device-test.XXXXXX').strip()
        if not re.fullmatch(r'/tmp/taq102-device-test\.[A-Za-z0-9]+', self.remote):
            raise RuntimeError('invalid remote temporary directory')
        self.ssh_run(f'ps > {self.remote}/before.ps; '
                     f'if test -e /tmp/glcube-test.log; then cp /tmp/glcube-test.log {self.remote}/previous-glcube-test.log; fi')
        self.restore_needed = True
        self.stop_apps()
        self.ssh_run('cat > /tmp/taq102-test.conf', b'brightness_auto=1\nbrightness=96\nsleep_minutes=0\n')
        stderr = (self.out / 'touchsim.stderr').open('wb')
        self.sim = subprocess.Popen(self.ssh + ['TOUCHSIM_NATIVE=1 /usr/bin/touchsim create'],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr, bufsize=0)
        ready, _, _ = select.select([self.sim.stdout], [], [], 12)
        line = self.sim.stdout.readline().decode().strip() if ready else ''
        match = re.fullmatch(r'TOUCH=(/dev/input/event\d+) POWER=(/dev/input/event\d+)', line)
        if not match:
            raise RuntimeError('touchsim discovery: ' + line)
        (self.out / 'devices.log').write_text(line + '\n')
        self.ssh_run(f'GLCUBE_TOUCH={match[1]} GLCUBE_POWER={match[2]} GLCUBE_TRACE=1 '
                     'GLCUBE_SETTINGS=/tmp/taq102-test.conf setsid /usr/bin/glcube '
                     '> /tmp/glcube-test.log 2>&1 < /dev/null &')
        self.wait_for(lambda: 'KMS up:' in self.trace(), 'KMS startup')
        time.sleep(4)  # Accelerometer orientation and the initial FPS interval settle.
        self.flipped = 'orientation: turned round' in self.trace()
        self.check('uinput and KMS ready', 'touch: declared 2048x1536' in self.trace(),
                   f'{line}, event_bytes=16, flipped={int(self.flipped)}, native_coordinates=1')
        self.check('Inter faces loaded', self.trace().count("glcube text:") == 5 and 'statusbar text: Inter' in self.trace(),
                   f'panel_faces={self.trace().count("glcube text:")}, bar_faces=1')

    def exercise(self):
        closed = self.capture('closed')
        self.performance('closed')
        self.panel(True)
        opened = self.capture('open')
        self.check('panel scanout', closed['sha256'] != opened['sha256'] and opened['glass_pixels'] > 1500,
                   f'changed={int(closed["sha256"] != opened["sha256"])}, glass_pixels={opened["glass_pixels"]}')
        self.performance('open')
        self.tap(772, 380)
        low = self.brightness()
        self.drag(772, 380, 772, 120, 3000, asynchronous=True)
        time.sleep(1.4)
        mid = self.brightness()
        self.capture('mid-drag')
        self.ack('OK drag')
        high = self.brightness()
        self.check('brightness follows drag', low < mid < high and high > 180,
                   f'low={low}, mid={mid}, high={high}')
        config = self.config()
        self.check('slider saves manual brightness', config['brightness_auto'] == '0' and int(config['brightness']) == high,
                   f'auto={config["brightness_auto"]}, saved={config["brightness"]}, sysfs={high}')
        self.tap(900, 100)
        self.check('Auto on', self.config()['brightness_auto'] == '1', 'brightness_auto=' + self.config()['brightness_auto'])
        self.tap(900, 100)
        self.check('Auto off', self.config()['brightness_auto'] == '0', 'brightness_auto=' + self.config()['brightness_auto'])
        self.tap(597, 310)
        self.check('15-minute segment', self.config()['sleep_minutes'] == '15', 'sleep_minutes=' + self.config()['sleep_minutes'])
        # Exactly one completed Rescue tap. Never send a confirmation or Loader tap.
        before = self.trace()
        self.tap(890, 278)
        time.sleep(.2)
        armed = self.capture('armed')
        self.command('sleep 3500')
        after = self.capture('disarmed')
        trace = self.trace()[len(before):]
        self.check('Rescue arms and expires', armed['rescue_amber_pixels'] > 1500 and after['rescue_amber_pixels'] < 100
                   and 'control centre: disarmed' in trace, f'armed_pixels={armed["rescue_amber_pixels"]}, '
                   f'disarmed_pixels={after["rescue_amber_pixels"]}, disarm_log={int("control centre: disarmed" in trace)}')
        self.performance('dragging', lambda: (self.drag(772, 380, 772, 120), self.drag(772, 120, 772, 380)))
        self.panel(False)
        self.performance('sliding', lambda: (self.panel(True), self.panel(False)))
        final = self.capture('closed-again')
        self.check('outside tap closes panel', final['glass_pixels'] < 100,
                   f'glass_pixels={final["glass_pixels"]}')
        # Exercise hold cancellation and two independent slots on the cube.
        x, y = self.point(500, 300)
        self.command(f'hold {x} {y} 700')
        a, b, c, d = [self.point(x, y) for x, y in ((350, 300), (650, 300), (200, 300), (800, 300))]
        coords = ' '.join(str(v) for point in (a, b, c, d) for v in point)
        self.command(f'pinch {coords} 1800')
        time.sleep(1.2)
        self.check('two-finger cube zoom', bool(re.search(r'slots 2: .*pinch_ref [1-9]', self.trace())),
                   'two_slot_trace=' + str(len(re.findall(r'slots 2:', self.trace()))))
        start = len(self.trace())
        self.command('power')
        self.wait_for(lambda: 'sleep' in self.trace()[start:].splitlines(), 'sleep')
        # This vendor fb0/blank accepts writes but returns an empty readback.
        dark = self.text('cat /sys/class/backlight/backlight/bl_power; cat /sys/class/graphics/fb0/blank')
        summary = self.text('cat /sys/kernel/debug/dri/0/summary')
        (self.out / 'sleep-summary.log').write_text(summary)
        self.check('power sleeps display', dark.split() in (['4'], ['4', '4']) and 'VOP [1010e000.vop]: DISABLED' in summary,
                   f'bl_power={dark.split()[0]}, fb_blank_readback={dark.split()[1:] or "unavailable"}, VOP_disabled={int("DISABLED" in summary)}')
        self.command('sleep 3000')
        self.command('power')
        self.wait_for(lambda: 'wake' in self.trace()[start:].splitlines(), 'wake')
        time.sleep(3)
        awake = self.text('cat /sys/class/backlight/backlight/bl_power; cat /sys/class/graphics/fb0/blank')
        self.check('power wakes display', awake.split() in (['0'], ['0', '0']), f'bl_power={awake.split()[0]}, fb_blank_readback={awake.split()[1:] or "unavailable"}')
        self.capture('awake')
        self.check('post-wake FPS/GL', '54.8 FPS' in self.trace()[start:] and 'glGetError 0x0' in self.trace()[start:],
                   'FPS=54.8, glGetError=0x0')

    def restore(self):
        if not self.restore_needed:
            return
        self.stop_apps()
        if self.sim and self.sim.poll() is None:
            try:
                self.command('quit'); self.sim.wait(timeout=5)
            except Exception:
                self.sim.terminate(); self.sim.wait(timeout=5)
        self.ssh_run(f'printf 0 > /sys/class/backlight/backlight/bl_power; '
                     f'printf 0 > /sys/class/graphics/fb0/blank; '
                     f'printf %s {self.original_brightness} > /sys/class/backlight/backlight/brightness')
        current = self.optional_file('/data/taq102.conf')
        if current != self.original:
            # Preserve unexpected data before restoring the captured preference file.
            if current is not None:
                self.ssh_run(f'cp /data/taq102.conf {self.remote}/unexpected-settings.conf')
            if self.original is None:
                self.ssh_run(f'mv /data/taq102.conf {self.remote}/created-settings.conf')
            else:
                self.ssh_run(f'cat > {self.remote}/restore-settings.conf', self.original)
                self.ssh_run(f'cp {self.remote}/restore-settings.conf /data/taq102.conf')
        if self.test_settings is None:
            self.ssh_run(f'mv /tmp/taq102-test.conf {self.remote}/test-settings.conf')
        else:
            self.ssh_run('cat > /tmp/taq102-test.conf', self.test_settings)
        log_bytes = int(self.text('wc -c < /data/log/taq102-app.log'))
        self.ssh_run('setsid /usr/bin/taq102-app >/tmp/taq102-supervisor-launch.log 2>&1 < /dev/null &')
        time.sleep(8)
        log = self.text(f'tail -c +{log_bytes + 1} /data/log/taq102-app.log')
        (self.out / 'supervisor-final.log').write_text(log)
        processes = self.text('ps')
        (self.out / 'processes-final.log').write_text(processes)
        self.check('supervisor restored', 'KMS up:' in log and log.count('54.8 FPS') >= 4 and '/usr/bin/taq102-app' in processes
                   and '/usr/bin/glcube' in processes, f'stable_FPS_intervals={log.count("54.8 FPS")}')
        current = self.optional_file('/data/taq102.conf')
        self.check('production preferences preserved', current == self.original,
                   f'exists={int(current is not None)}, before/after_sha256=' + hashlib.sha256(current or b'').hexdigest())
        self.restore_needed = False

    def run(self):
        signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
        try:
            self.prepare(); self.exercise()
        except (Exception, KeyboardInterrupt) as error:
            self.failures += 1
            message = f'FAIL execution: {error}'
            print(message, flush=True); self.log.write(message + '\n'); self.log.flush()
        finally:
            try:
                if self.restore_needed:
                    try:
                        self.trace()
                    except Exception as error:
                        print(f'Trace collection failed: {error}', flush=True)
                self.restore()
            except Exception as error:
                self.failures += 1
                message = f'FAIL restoration: {error}'
                print(message, flush=True); self.log.write(message + '\n'); self.log.flush()
        print(f'Evidence: {self.out}', flush=True)
        return int(self.failures != 0)


if __name__ == '__main__':
    sys.exit(DeviceTest().run())
