#!/usr/bin/env python3
"""Record the device's video and audio to one mp4, so a bisection result can be
watched rather than taken on trust.

Far simpler than the E2E recorder in tests/e2e/lib/recorder.py: there is no
harness pane, no run log and no suite timeline, because a bisection has only one
subject. The stream decoding is that module's, through tests/e2e/lib/streams.py.

    ./record_run.py --host u64 --seconds 25 -o runs/c4be69a2.mp4

Video is written straight to an encoder as it arrives; audio is small enough to
hold in memory and is muxed in at the end. Feeding one ffmpeg process both from
one thread deadlocks at this frame size, which is why the E2E recorder uses a
process per stream and why this one buffers instead.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
import wave
from pathlib import Path

import numpy as np

SCRIPT_DIR = Path(__file__).resolve().parent          # tests/bisect/doom
sys.path.insert(0, str(SCRIPT_DIR.parents[1] / "lib"))          # tests/lib
sys.path.insert(0, str(SCRIPT_DIR.parents[1] / "e2e" / "lib"))  # tests/e2e/lib

import streams                                                     # noqa: E402
import targets                                                     # noqa: E402
from api import UltimateApi                                        # noqa: E402

sys.path.insert(0, str(SCRIPT_DIR))
import doomlib as D                                                # noqa: E402

PAL_FPS = 50


def encode(host, seconds, out_path, want_audio, quality, api):
    handle = targets.resolve(host)
    addresses = streams.source_addresses(host)
    video_sock = streams.stream_socket(handle.video_group, handle.video_port, timeout=2.0)
    audio_sock = None
    if want_audio:
        audio_sock = streams.stream_socket(handle.audio_group, handle.audio_port,
                                           timeout=2.0)
    assembler = streams.FrameAssembler()
    timeline = streams.AudioTimeline()
    pcm = bytearray()
    encoder = None
    frames = 0
    tmp_video = f"{out_path}.video.mp4"
    tmp_audio = f"{out_path}.audio.wav"

    socks = [s for s in (video_sock, audio_sock) if s is not None]
    try:
        for sock, data, mine in streams.receive(socks, addresses, seconds):
            if not mine:
                continue
            if audio_sock is not None and sock is audio_sock:
                pcm += timeline.push(data).pcm
                continue
            frame = assembler.push(data)
            if frame is None:
                continue
            rgb = D.PALETTE[np.frombuffer(streams.unpack(frame.packed), np.uint8)
                            .reshape(frame.height, frame.width)]
            if encoder is None:
                encoder = subprocess.Popen(
                    ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                     "-f", "rawvideo", "-pix_fmt", "rgb24",
                     "-s", f"{frame.width}x{frame.height}", "-r", str(PAL_FPS),
                     "-i", "pipe:0", "-c:v", "libx264", "-preset", quality,
                     "-pix_fmt", "yuv420p", tmp_video],
                    stdin=subprocess.PIPE)
            encoder.stdin.write(rgb.tobytes())
            frames += 1
    finally:
        for sock in socks:
            sock.close()
        if encoder is not None:
            encoder.stdin.close()
            encoder.wait(timeout=120)

    if frames == 0:
        raise RuntimeError("no video frames arrived; is the stream running?")

    if want_audio and pcm:
        rate = streams.rate_for(api.configs.get("U64 Specific Settings",
                                                "System Mode") != "NTSC")
        with wave.open(tmp_audio, "wb") as handle_wav:
            handle_wav.setnchannels(2)
            handle_wav.setsampwidth(2)
            handle_wav.setframerate(int(round(rate)))
            handle_wav.writeframes(bytes(pcm))
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-i", tmp_video, "-i", tmp_audio,
             "-c:v", "copy", "-c:a", "aac", "-b:a", "128k",
             "-shortest", out_path], check=True)
        os.remove(tmp_video)
        os.remove(tmp_audio)
    else:
        os.replace(tmp_video, out_path)
    return frames, len(pcm) // 4


def main():
    ap = argparse.ArgumentParser(
        description="Record the device's video and audio to an mp4.")
    ap.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    ap.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    ap.add_argument("-t", "--timeout", type=float, default=30.0)
    ap.add_argument("--seconds", type=float, default=25.0,
                    help="how long to record")
    ap.add_argument("-o", "--out", required=True, help="output mp4")
    ap.add_argument("--no-record-audio", dest="record_audio", action="store_false",
                    help="video only")
    ap.add_argument("--record-quality", default="veryfast",
                    help="x264 preset; 'veryfast' keeps up with the stream")
    args = ap.parse_args()

    if not shutil.which("ffmpeg"):
        print("ffmpeg is not on PATH; cannot record", file=sys.stderr)
        return 2
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)

    api = UltimateApi(args.host, args.password or None, args.timeout)
    started = time.monotonic()
    with streams.Arming(api, args.host) as arming:
        arming.start("video")
        if args.record_audio:
            arming.start("audio")
        frames, samples = encode(args.host, args.seconds, args.out,
                                 args.record_audio, args.record_quality, api)
    print(f"RECORDED {args.out} frames={frames} audio_samples={samples} "
          f"seconds={time.monotonic() - started:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
