# Look for brightness flicker rather than displacement.
#
# The panel runs at 56 Hz and the camera at 30, so a frame-to-frame
# alternation is aliased and invisible in a single frame mean. The rolling
# shutter is the way in: it samples successive rows at successive instants,
# so any flicker faster than the frame rate shows up as horizontal banding
# whose phase walks between frames. Report both the global variation and the
# banding.
#   flicker.py <video> [frames] [crop w:h:x:y]
import sys, subprocess, numpy as np

vid = sys.argv[1]
nf = int(sys.argv[2]) if len(sys.argv) > 2 else 60
crop = sys.argv[3] if len(sys.argv) > 3 else None
probe = subprocess.run(['ffprobe','-v','error','-select_streams','v','-show_entries',
                        'stream=width,height','-of','csv=p=0:nk=1',vid],
                       capture_output=True,text=True).stdout.strip()
vals = [int(v) for v in probe.split(',') if v.strip().isdigit()]
W, H = vals[0], vals[1]
w, h, x, y = (map(int, crop.split(':')) if crop else (W//2, H//2, W//4, H//4))
raw = subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-i',vid,'-vf',
                      f'crop={w}:{h}:{x}:{y}','-frames:v',str(nf),'-f','rawvideo',
                      '-pix_fmt','gray','-'],capture_output=True).stdout
n = len(raw)//(w*h)
f = np.frombuffer(raw, np.uint8)[:n*w*h].reshape(n, h, w).astype(np.float32)
print(f'{vid.split("/")[-1]}: {n} frames of {w}x{h}')

frame_mean = f.mean(axis=(1,2))
print(f'  whole-frame brightness: mean {frame_mean.mean():.2f}, '
      f'variation {frame_mean.std():.3f} ({100*frame_mean.std()/frame_mean.mean():.2f}%), '
      f'range {frame_mean.max()-frame_mean.min():.2f}')

rows = f.mean(axis=2)                       # per frame, luminance down the frame
rows_flat = rows - rows.mean(axis=1, keepdims=True)
band = rows_flat.std(axis=1)
print(f'  rolling-shutter banding within a frame: {band.mean():.3f} levels rms '
      f'({100*band.mean()/frame_mean.mean():.2f}% of mean)')

# No frequency is reported. The dominant FFT bin of a short crop is the lens
# vignetting and the panel's own brightness gradient, not an oscillation, and
# reading it as a frequency gave 96 Hz on one crop and 432 Hz on another of the
# same recording. What is trustworthy is the amplitude, compared against a crop
# of the black bezel: it emits nothing, so whatever bands there is the room.
# Mains light in Spain flickers at 100 Hz and lands on both.
