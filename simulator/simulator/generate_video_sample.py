"""Generate original moving test patterns using the installed OpenCV encoder."""
import argparse
from pathlib import Path
import cv2
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--codec", default="avc1", choices=["avc1", "hvc1", "mp4v"])
args = parser.parse_args()
if args.output.exists():
    raise SystemExit("Output already exists")
writer = cv2.VideoWriter(str(args.output), cv2.VideoWriter_fourcc(*args.codec), 30, (960, 540))
if not writer.isOpened():
    raise SystemExit("Requested encoder unavailable in this OpenCV installation")
try:
    for index in range(300):
        frame = np.zeros((540, 960, 3), dtype=np.uint8)
        frame[:] = (30, 20, 12)
        for x in range(0, 960, 80):
            cv2.line(frame, (x, 0), (x, 539), (65, 55, 35), 1)
        cv2.circle(frame, (80 + index * 3 % 800, 280), 48, (210, 210, 40), -1)
        cv2.putText(frame, "RM27 LOCAL VIDEO TEST", (60, 85), cv2.FONT_HERSHEY_SIMPLEX, 1, (230, 240, 240), 2)
        cv2.putText(frame, f"{args.codec} / frame {index:03d} / 30 FPS", (60, 450), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (230, 240, 240), 2)
        writer.write(frame)
finally:
    writer.release()
reader = cv2.VideoCapture(str(args.output))
valid, _ = reader.read()
reader.release()
if not valid:
    raise SystemExit("Encoder produced no readable frame; sample is invalid")
print(args.output)
