# Measure the horizontal displacement of a vertical-line pattern, row by row.
#
# The panel shows single-pixel vertical lines, so every camera row carries a
# strong spatial tone. Its phase is where that row's lines sit horizontally;
# differentiating the phase down the frame gives the wobble the eye reads as a
# wave. Amplitude comes out in panel pixels, and the vertical period says what
# frequency modulates it: one period of N rows at a 35.4 kHz line rate is
# 35400/N Hz.
#   zigzag.py <video> [frames] [crop w:h:x:y]
import sys, subprocess, numpy as np

vid = sys.argv[1]
nframes = int(sys.argv[2]) if len(sys.argv) > 2 else 12
crop = sys.argv[3] if len(sys.argv) > 3 else None
probe = subprocess.run(['ffprobe','-v','error','-select_streams','v','-show_entries','stream=width,height','-of','csv=p=0:nk=1',vid],capture_output=True,text=True).stdout.strip()
vals = [int(v) for v in probe.split(',') if v.strip().isdigit()]
W, H = vals[0], vals[1]
if crop:
    w, h, x, y = map(int, crop.split(':'))
else:   # the middle half of the frame, away from the bezel
    w, h, x, y = W//2, H//2, W//4, H//4
raw = subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-i',vid,'-vf',f'crop={w}:{h}:{x}:{y}','-frames:v',str(nframes),'-f','rawvideo','-pix_fmt','gray','-'],capture_output=True).stdout
n = len(raw)//(w*h)
f = np.frombuffer(raw, np.uint8)[:n*w*h].reshape(n, h, w).astype(np.float32)
print(f'{vid.split("/")[-1]}: {n} frames of {w}x{h} (crop {x},{y} of {W}x{H})')

# The pattern's spatial frequency, from the mean row spectrum of frame 0.
row = f[0] - f[0].mean(axis=1, keepdims=True)
spec = np.abs(np.fft.rfft(row * np.hanning(w), axis=1)).mean(axis=0)
# Look only where the line pattern can be: between 3 and 30 camera pixels per
# pair of lines. Below that band sits the lens vignetting and the panel's own
# brightness gradient, which are stronger than the pattern and would win.
lo, hi = max(3, int(w/30)), min(len(spec) - 1, int(w/3))
k = int(np.argmax(spec[lo:hi]) + lo)
px_per_cycle = w / k
print(f'  line pattern at bin {k}: {px_per_cycle:.2f} camera px per pair of lines')

# Two different things look alike in one frame: a fixed bend in the panel's
# own geometry, which the eye never notices, and a displacement that changes
# from frame to frame, which is what reads as a wave. Measure both.
profiles = []
for i in range(n):
    rows = f[i] - f[i].mean(axis=1, keepdims=True)
    ft = np.fft.rfft(rows * np.hanning(w), axis=1)[:, k]
    disp = np.unwrap(np.angle(ft)) / (2*np.pi) * 2.0      # 1 cycle = 2 panel px
    disp -= np.polyval(np.polyfit(np.arange(h), disp, 1), np.arange(h))
    profiles.append(disp)
P = np.array(profiles)
spatial = P.mean(axis=0).std()
temporal = P.std(axis=0).mean()
print(f'  fixed bend {spatial:.2f} panel px, MOVING {temporal:.3f} panel px rms '
      f'(peak-to-peak {(P.max(axis=0) - P.min(axis=0)).mean():.2f}) over {n} frames')
