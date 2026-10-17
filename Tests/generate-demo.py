"""Generate an original, deterministic 4-bar test loop; no external samples."""
import array
import math
from pathlib import Path
import random
import wave

rate = 48000
duration = 8
frames = rate * duration
left = [0.0] * frames
right = [0.0] * frames
rng = random.Random(4217)
tau = math.tau


def note(midi, start, length, level, pan=0):
    frequency = 440 * 2 ** ((midi - 69) / 12)
    first = int(start * rate)
    for i in range(min(int(length * rate), frames - first)):
        t = i / rate
        env = min(1, t / 0.004) * math.exp(-t * 4.8)
        release = min(1, (length - t) / 0.08)
        value = level * env * release * (
            math.sin(tau * frequency * t)
            + 0.20 * math.sin(tau * frequency * 2.002 * t) * math.exp(-t * 7)
            + 0.08 * math.sin(tau * frequency * 3.998 * t) * math.exp(-t * 10)
        )
        left[first + i] += value * (1 - pan * 0.4)
        right[first + i] += value * (1 + pan * 0.4)


chords = [(60, 63, 67, 70), (56, 60, 63, 67), (55, 58, 62, 65), (58, 62, 65, 67)]
for bar, chord in enumerate(chords):
    for step, degree in enumerate([0, 2, 1, 3, 2, 1, 3, 2]):
        note(chord[degree], bar * 2 + step * 0.25, 0.9, 0.16, (-1 if step % 2 else 1) * 0.55)
    note(chord[0] - 24, bar * 2, 0.8, 0.22)
    note(chord[0] - 24, bar * 2 + 1.25, 0.6, 0.18)

for beat in range(16):
    start = int(beat * 0.5 * rate)
    if beat % 4 in (0, 2):
        for i in range(min(int(rate * 0.3), frames - start)):
            t = i / rate
            phase = tau * (46 * t + 56 * 0.025 * (1 - math.exp(-t / 0.025)))
            hit = 0.29 * math.sin(phase) * math.exp(-t * 19)
            left[start + i] += hit
            right[start + i] += hit
    if beat % 4 in (1, 3):
        previous = 0
        for i in range(min(int(rate * 0.16), frames - start)):
            t = i / rate
            white = rng.uniform(-1, 1)
            hit = (white - previous * 0.65) * math.exp(-t * 42) * 0.065
            previous = white
            left[start + i] += hit
            right[start + i] += hit * 0.9
    for offbeat in (0, 0.25):
        first = start + int(offbeat * rate)
        previous = 0
        for i in range(min(int(rate * 0.055), frames - first)):
            white = rng.uniform(-1, 1)
            hat = (white - previous) * math.exp(-i / rate * 110) * 0.018
            previous = white
            left[first + i] += hat * 0.8
            right[first + i] += hat

peak = max(max(map(abs, left)), max(map(abs, right)))
pcm = array.array("h")
for l, r in zip(left, right):
    pcm.extend([int(l / peak * 0.52 * 32767), int(r / peak * 0.52 * 32767)])
path = Path(__file__).resolve().parents[1] / "build/qa/alpha-demo.wav"
path.parent.mkdir(parents=True, exist_ok=True)
with wave.open(str(path), "wb") as output:
    output.setnchannels(2)
    output.setsampwidth(2)
    output.setframerate(rate)
    output.writeframes(pcm.tobytes())
print(path)
