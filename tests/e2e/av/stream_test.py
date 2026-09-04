#!/usr/bin/env python3
"""Short C64U audio/video stream smoke test with assembled SID stimuli."""

import argparse
import math
import os
import sys
import time
from pathlib import Path
from collections.abc import Sequence


# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402


from api import UltimateApi
from assembler import assemble
from av_stream import (
    AUDIO_PACKET_BYTES,
    VIDEO_PACKET_BYTES,
    AvStreamCapture,
    audio_samples,
    first_bright_frame,
    first_loud_packet,
    packet_sequence,
    video_frames,
)
from report import Failure, check, detail, suite_fail, suite_ok

SCRIPT_DIR = Path(__file__).resolve().parent


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


def ladder_audio(capture: AvStreamCapture) -> list[int]:
    """One channel of every audio sample the capture holds, in order.

    The whole capture rather than a slice from the first loud packet: where the
    ladder starts inside it is decided by find_ladder_start, which matches the
    signal rather than its amplitude.
    """
    samples: list[int] = []
    for packet in capture.audio_packets:
        samples.extend(audio_samples(packet)[::2])
    return samples


def find_ladder_start(samples: Sequence[int], slot: int, window: int) -> int:
    """The sample offset at which the ladder's first note begins.

    tone_ladder.asm waits 100 frames before its first note and then plays the
    fifteen once, so the capture holds a lead-in of roughly two seconds and
    then three seconds of ladder. Anchoring on the first packet above an RMS
    threshold put the start inside that lead-in: measured on an Ultimate 64
    Elite in PAL, every note came back two slots late, so the suite read
    164.8Hz where it expected 130.8Hz and the fifteen detected frequencies were
    the expected fifteen shifted by two places.

    Amplitude cannot separate the lead-in from the ladder, so the first note's
    own frequency does it instead: the offset taken is the one whose first slot
    carries the most 130.8Hz. Only the anchor is chosen this way. Every note
    including the first is then checked at that offset, so a device that played
    a different ladder still fails.
    """
    latest = len(samples) - len(LADDER_FREQUENCIES) * slot
    if latest < 0:
        raise Failure("tone ladder capture is shorter than the ladder itself")
    step = max(1, window // 16)
    centre = (slot - window) // 2

    def power_at(offset: int) -> float:
        return goertzel_power(samples[offset + centre:offset + centre + window],
                              LADDER_FREQUENCIES[0])

    return max(range(0, latest + 1, step), key=power_at)


def assert_tone_ladder(capture: AvStreamCapture) -> None:
    samples = ladder_audio(capture)
    slot_samples = round(PAL_AUDIO_RATE * LADDER_FRAMES_PER_NOTE / 50.0)
    window = round(PAL_AUDIO_RATE * 0.10)
    anchor = find_ladder_start(samples, slot_samples, window)
    detail(f"ladder starts {anchor / PAL_AUDIO_RATE:.2f}s into the capture")
    detected = []
    for index, expected in enumerate(LADDER_FREQUENCIES):
        offset = anchor + index * slot_samples + (slot_samples - window) // 2
        window_samples = samples[offset:offset + window]
        if len(window_samples) != window:
            # find_ladder_start bounds the anchor so every note fits, so this
            # is unreachable unless that bound is changed; it stays as the
            # statement of what the loop needs.
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
        device.runners.upload("run_prg", program)
        capture.capture(1.5)
        capture.clear()
        # 4.5s against the ladder's own 3.0s. tone_ladder.asm waits 100 frames
        # (2.0s) before its first note and the spool is cleared 1.5s after the
        # upload, so the ladder starts about half a second into this window;
        # measured on an Ultimate 64 Elite in PAL, at 0.42s to 0.44s over four
        # runs. The rest is what find_ladder_start has to move in, and it
        # covers a program that takes a second longer to start than measured.
        capture.capture(4.5)
        log_packet_health(capture)
        assert_tone_ladder(capture)
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
    # -H is the stream source rather than the device under test, so
    # U64_C64_HOST comes first; the password is the ordinary one.
    parser.add_argument(
        "-H", "--host",
        default=os.environ.get("U64_C64_HOST", cli.host_default()),
        help="C64U stream source")
    parser.add_argument("-p", "--password", default=cli.password_default(),
                        help=f"REST password (default: ${cli.DEFAULT_PASSWORD_ENV})")
    parser.add_argument("--case", choices=("all", "ladder", "pop"), default="all")
    args = parser.parse_args()
    device = UltimateApi(args.host, args.password or None)
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
