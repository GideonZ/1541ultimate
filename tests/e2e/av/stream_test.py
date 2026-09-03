#!/usr/bin/env python3
"""Short C64U audio/video stream smoke test with assembled SID stimuli."""

import argparse
import math
import os
import sys
import time
from pathlib import Path
from typing import Iterable, List, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[2]

sys.path.insert(0, str(REPO_ROOT / "tests" / "lib"))
sys.path.insert(0, str(REPO_ROOT / "tests" / "e2e" / "lib"))

from api import UltimateApi
from assembler import assemble
from av_stream import (
    AUDIO_PACKET_BYTES,
    VIDEO_PACKET_BYTES,
    AvStreamCapture,
    Packet,
    audio_samples,
    first_bright_frame,
    first_loud_packet,
    packet_sequence,
    video_frames,
)
from report import Failure, check, detail, suite_fail, suite_ok


PAL_AUDIO_RATE = 47982.8869047619
LADDER_FRAMES_PER_NOTE = 10
LADDER_FREQUENCIES = (130.8, 146.8, 164.8, 174.6, 196.0, 220.0, 246.9, 261.6,
                      246.9, 220.0, 196.0, 174.6, 164.8, 146.8, 130.8)


def log_packet_health(capture: AvStreamCapture) -> None:
    audio = packet_sequence(capture.audio_packets, "audio", AUDIO_PACKET_BYTES)
    video = packet_sequence(capture.video_packets, "video", VIDEO_PACKET_BYTES)
    detail(f"audio packets={audio.packets} missing={audio.missing} reordered={audio.reordered}")
    detail(f"video packets={video.packets} missing={video.missing} reordered={video.reordered}")


def goertzel_power(samples: Sequence[int], frequency: float) -> float:
    coefficient = 2.0 * math.cos(2.0 * math.pi * frequency / PAL_AUDIO_RATE)
    previous = 0.0
    previous2 = 0.0
    for sample in samples:
        current = sample + coefficient * previous - previous2
        previous2 = previous
        previous = current
    return previous2 * previous2 + previous * previous - coefficient * previous * previous2


def ladder_audio(capture: AvStreamCapture, start: float) -> List[int]:
    first = first_loud_packet(capture.audio_packets, start)
    index = capture.audio_packets.index(first)
    samples: List[int] = []
    for packet in capture.audio_packets[index:]:
        samples.extend(audio_samples(packet)[::2])
    return samples


def assert_tone_ladder(capture: AvStreamCapture, start: float) -> None:
    samples = ladder_audio(capture, start)
    slot_samples = round(PAL_AUDIO_RATE * LADDER_FRAMES_PER_NOTE / 50.0)
    window = round(PAL_AUDIO_RATE * 0.10)
    detected = []
    for index, expected in enumerate(LADDER_FREQUENCIES):
        offset = index * slot_samples + (slot_samples - window) // 2
        window_samples = samples[offset:offset + window]
        if len(window_samples) != window:
            raise Failure("tone ladder capture ended before all notes arrived")
        actual = max(LADDER_FREQUENCIES, key=lambda frequency: goertzel_power(window_samples, frequency))
        detected.append(actual)
        if abs(actual - expected) > 1.0:
            raise Failure(f"tone ladder note {index} expected {expected:.1f}Hz, detected {actual:.1f}Hz")
    detail("tone ladder Hz=" + ",".join(f"{frequency:.1f}" for frequency in detected))


def run_tone_ladder(device: UltimateApi) -> None:
    device.machine.reset(force=True)
    program = assemble(SCRIPT_DIR / "tone_ladder.asm")
    # The handle rather than the host name: for a cartridge target the video,
    # the audio and the request that starts them belong to the computer, and
    # only the handle knows which machine that is.
    with AvStreamCapture(device.target) as capture:
        capture.capture(0.15)
        started = time.monotonic()
        device.runners.upload("run_prg", program)
        capture.capture(1.5)
        capture.clear()
        capture.capture(3.5)
        log_packet_health(capture)
        assert_tone_ladder(capture, started)
        colors = set()
        for frame in video_frames(capture.video_packets):
            colors.add(frame.colors().most_common(1)[0][0])
        if len(colors) < 8:
            raise Failure(f"tone ladder video showed only {len(colors)} background colours")


def run_key_pop(device: UltimateApi) -> None:
    device.machine.reset(force=True)
    program = assemble(SCRIPT_DIR / "av_pop_key.asm")
    with AvStreamCapture(device.target) as capture:
        capture.capture(0.15)
        device.runners.upload("run_prg", program)
        capture.capture(1.5)
        capture.clear()
        pressed = time.monotonic()
        device.machine.press("space")
        capture.capture(0.60)
        log_packet_health(capture)
        frames = video_frames(capture.video_packets)
        bright = first_bright_frame(frames, pressed)
        loud = first_loud_packet(capture.audio_packets, pressed)
        video_latency = bright.received_at - pressed
        audio_latency = loud.received_at - pressed
        offset = audio_latency - video_latency
        detail(f"key-to-video={video_latency * 1000:.1f}ms key-to-audio={audio_latency * 1000:.1f}ms A/V={offset * 1000:.1f}ms")
        if video_latency < 0 or audio_latency < 0 or max(video_latency, audio_latency) > 1.5:
            raise Failure("key-triggered A/V marker arrived too late")
        if abs(offset) > 0.25:
            raise Failure(f"key-triggered A/V marker offset is {offset * 1000:.1f}ms")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_C64_HOST", os.environ.get("U64_HOST", "u64")),
                        help="C64U stream source")
    parser.add_argument("--case", choices=("all", "ladder", "pop"), default="all")
    args = parser.parse_args()
    device = UltimateApi(args.host)
    try:
        if args.case in ("all", "ladder"):
            with check("tone ladder reaches audio and video streams"):
                run_tone_ladder(device)
        if args.case in ("all", "pop"):
            with check("Space key reaches aligned audio and video pop"):
                run_key_pop(device)
    except (Failure, OSError) as exc:
        suite_fail("stream_test", str(exc))
        return 1
    suite_ok("stream_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
