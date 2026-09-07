# Per-frame translation of a cropped region relative to the first frame, by
# phase correlation. Exposure changes do not move the peak; a real shift does.
#   python3 shift.py <video> <crop w:h:x:y>
import sys, subprocess, numpy as np
vid, crop = sys.argv[1], sys.argv[2]
w, h, x, y = map(int, crop.split(':'))
raw = subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-i',vid,'-vf',f'select=gte(n\\,15),crop={w}:{h}:{x}:{y}','-f','rawvideo','-pix_fmt','gray','-'],capture_output=True).stdout
n = len(raw)//(w*h); f = np.frombuffer(raw, np.uint8)[:n*w*h].reshape(n,h,w).astype(np.float32)
win = np.outer(np.hanning(h), np.hanning(w))
def prep(a): a = a - a.mean(); return np.fft.fft2(a*win)
F0 = prep(f[0]); res=[]
for i in range(n):
    Fi = prep(f[i]); R = F0*np.conj(Fi); R /= np.abs(R)+1e-6
    c = np.fft.ifft2(R).real; p = np.unravel_index(np.argmax(c), c.shape)
    dy = p[0] if p[0] <= h//2 else p[0]-h; dx = p[1] if p[1] <= w//2 else p[1]-w
    res.append((i, dx, dy, f[i].mean()))
dxs = np.array([r[1] for r in res]); dys = np.array([r[2] for r in res])
print(f'{vid.split("/")[-1]}: frames {n}, |dx|>=1 in {int((abs(dxs)>=1).sum())}, |dy|>=1 in {int((abs(dys)>=1).sum())}, max |dx| {int(abs(dxs).max())} max |dy| {int(abs(dys).max())}')
print(' '.join(f'{r[1]:+d}/{r[2]:+d}' for r in res))
