import argparse
import os
import socket

import recorder as recorder_lib
import report
import syslog_collector
import targets as targets_lib
from api import UltimateApi
from report import Failure
from runtests.device import Device
from runtests.exits import die
from runtests.identity import git_answer


# How long the recorder's own requests wait. Short, because both of them are
# issued from a loop that has to keep draining the device's streams, and short
# again because a failure there is an answer: the next slot asks again.
RECORDER_TIMEOUT_SECONDS = 2.0

# What the recorder writes when nothing says otherwise. The rate is well below
# the device's 50 Hz because the material is a screen that is static for
# seconds at a time and then changes completely in one frame: encoding an
# unchanged screen fifty times a second costs a reader nothing and costs the
# host, which is a Raspberry Pi, everything. The keyframe interval bounds how
# far a player decodes back when it steps backwards through a stretch where no
# scene cut fired.
RECORD_FPS = 10
RECORD_KEYINT_SECONDS = 1.0


# Every recorder option except --record itself, and what makes each one a
# non-default. A run given one of these without --record would otherwise
# produce no recording and no complaint, which is the shape of mistake that
# costs a whole gate run to discover.
RECORDING_OPTIONS = (
    ("record_video", True, "--no-record-video"),
    ("record_audio", True, "--no-record-audio"),
    ("record_menu", True, "--no-record-menu"),
    ("record_stamp", True, "--no-record-stamp"),
    ("record_menu_min_interval_ms", 0, "--record-menu-min-interval-ms"),
    ("record_layout", "combined", "--record-layout"),
    ("record_quality", "lossless", "--record-quality"),
    ("record_scale", 1, "--record-scale"),
    ("record_fps", RECORD_FPS, "--record-fps"),
    ("record_keyint", RECORD_KEYINT_SECONDS, "--record-keyint"),
    ("record_ffmpeg_args", "", "--record-ffmpeg-args"),
)


def check_recording_options(args: argparse.Namespace) -> None:
    """Refuse a recorder option given without --record, before the run starts."""
    if not args.record:
        given = [flag for name, default, flag in RECORDING_OPTIONS
                 if getattr(args, name) != default]
        if given:
            die(f"{', '.join(given)} needs --record; without it nothing is "
                "recorded and these do nothing")
        return
    if not (args.record_video or args.record_audio or args.record_menu):
        die("--record with every source dropped records nothing; keep at least "
            "one of the video, the audio and the harness pane")
    if args.record_scale < 1:
        die("--record-scale is an integer factor of 1 or more; a fractional "
            "scale resamples pixel art")
    if args.record_fps < 1:
        die("--record-fps must be at least 1")
    if not args.output_dir:
        die("--record needs -o DIR to write the recording into")


def start_recorder(args: argparse.Namespace, target: targets_lib.Target,
                   device: "Device") -> "recorder_lib.Recorder | None":
    """Open this target's recording, or say why it did not open.

    In the process that owns one target: the output is per target, multicast
    is delivered to every socket that joined, and one per target needs no
    coordination.
    """
    if not (args.record and args.output_dir):
        return None
    options = recorder_lib.Options(
        video=args.record_video, audio=args.record_audio, menu=args.record_menu,
        stamp=args.record_stamp, layout=args.record_layout,
        quality=args.record_quality, scale=args.record_scale, fps=args.record_fps,
        keyint=args.record_keyint,
        menu_min_interval_ms=args.record_menu_min_interval_ms,
        ffmpeg_args=args.record_ffmpeg_args)
    # Its own client rather than the runner's probe. The recorder's PUTs run
    # on its own thread and would advance the probe's mutation counter, which
    # api.MachineApi.reset compares against to decide whether the device has
    # been touched since the last reset.
    api = UltimateApi(target, device.password, RECORDER_TIMEOUT_SECONDS)
    made = recorder_lib.Recorder(args.output_dir, target, api, options,
                                 identity=recording_identity(target, device))
    problem = made.start()
    if problem:
        report.warn(f"recording: {problem}")
        return None
    report.detail("recording:  " + ", ".join(made.files))
    return made


def recording_identity(target: targets_lib.Target,
                       device: "Device") -> dict[str, str]:
    """What every frame's stamp and the title card say about this run.

    A single frame travels: somebody screenshots a failure into an issue,
    somebody shares the video, an agent is handed one still. Any of those has
    to answer which device, which firmware, which run and when, without the
    file it came from.
    """
    found = {"target": target.token, "host": socket.gethostname()}
    for address in sorted(syslog_collector.resolve(target.video_host)):
        found["address"] = address
        break
    try:
        info = device.probe.info()
        found["firmware"] = f"{info.product} {info.firmware_version}"
        found["fpga"] = info.fpga_version
    except Failure:
        # A device that will not say is not a reason to record nothing.
        pass
    for name, variable in (("ci", "GITHUB_RUN_ID"), ("branch", "GITHUB_REF_NAME")):
        value = os.environ.get(variable)
        if value:
            found[name] = value
    if "branch" not in found:
        branch = git_answer("rev-parse", "--abbrev-ref", "HEAD")
        if branch:
            found["branch"] = branch
    commit = os.environ.get("GITHUB_SHA") or git_answer("rev-parse", "--short",
                                                        "HEAD")
    if commit:
        found["commit"] = commit
    # Whether the firmware under test was built from that commit or from a
    # working tree with changes in it. A recording of a modified tree is not
    # evidence about the commit it names, and nothing else on the card says so.
    changes = git_answer("status", "--porcelain")
    if changes is not None:
        found["dirty"] = "modified" if changes.strip() else "clean"
    return {name: value for name, value in found.items() if value}


def stop_recorder(made: "recorder_lib.Recorder | None",
                  directory: str, token: str) -> None:
    """End the recording, write its sidecars and record its own health."""
    if made is None:
        return
    capture = made.stop()
    problems, sidecars = recorder_lib.finish(directory, token,
                                             made.started_wall, made.lead_in,
                                             made.files,
                                             audio=made.audio_path())
    for problem in problems:
        report.warn(f"recording: {problem}")
    if problems:
        capture.setdefault("problems", []).extend(problems)
    # The subtitles are written after the recorder stopped, so the record it
    # produced does not know about them yet. OBS-8.11 asks the record to name
    # every output file it wrote, and a reader who cannot see the sidecar in
    # the record has no reason to look for it.
    capture["files"] = sorted(set(capture.get("files") or []) | set(sidecars))
    report.capture_result(**capture)
    report.detail(f"recording:  {capture['frames']} frames, "
                  f"{capture['frames_shed']} shed")
