import argparse
import os
import shutil
import sys

import cabling
import profiles
import report
import syslog_collector
import targets as targets_lib
from runtests.constants import MODES
from runtests.device import DEFAULT_RECOVER_MAX_PER_SUITE, DEFAULT_RECOVER_MAX_TOTAL, DEFAULT_RECOVER_TIMEOUT_SECONDS
from runtests.exits import EXIT_DEVICE_UNHEALTHY, EXIT_OK, EXIT_RECOVERED, EXIT_RETRIED, EXIT_SUITE_FAILED, EXIT_USAGE, die
from runtests.recording import RECORD_FPS, RECORD_KEYINT_SECONDS


DEFAULT_HOST = os.environ.get("U64_HOST", "u64")
DEFAULT_PASSWORD = os.environ.get("U64_PASS", "")
DEFAULT_TIMEOUT = "30.0"

# How many times a suite may run in total, counting the first. The flag counts
# executions rather than retries, so this number and the highest `attempt`
# value that can appear in the JSONL and the highest `attempt-<n>` directory
# are one number with one meaning.
DEFAULT_ATTEMPTS = 3


# The help page, on one grid: every heading is flush left, lower case and
# followed by a colon, which is what argparse renders for its own argument
# groups, and every body line sits two columns in. argparse passes the
# description and the epilog through untouched, so those are written to the
# same shape by hand.
HELP_WIDTH = 80               # the page never uses more columns than this
HELP_INDENT = 2               # where a section's body starts
HELP_DESCRIPTION_COLUMN = 26  # where an option's description starts


class HelpFormatter(argparse.RawDescriptionHelpFormatter):
    """Write each option the way a GNU tool does.

    argparse repeats the value's name once per spelling, as in
    "-s NAME, --suite NAME" and "-m LIST, --mode LIST, --modes LIST". That
    is hard to read and it pushes the descriptions across the page. This
    names the value once, after the last spelling, and holds the whole page
    to one width whatever the terminal's is.
    """

    def __init__(self, prog, indent_increment=HELP_INDENT,
                 max_help_position=HELP_DESCRIPTION_COLUMN, width=None):
        if width is None:
            width = min(shutil.get_terminal_size().columns, HELP_WIDTH) - 2
        super().__init__(prog, indent_increment, max_help_position, width)

    def _format_action_invocation(self, action) -> str:
        if not action.option_strings:                      # a positional
            return self._metavar_formatter(action, action.dest)(1)[0]
        if action.nargs == 0:                              # a flag
            return ", ".join(action.option_strings)
        value = self._format_args(
            action, self._get_default_metavar_for_optional(action))
        spellings = list(action.option_strings)
        last = spellings.pop()
        # "--suite=NAME", as GNU writes it; "-s NAME" for a short spelling.
        spellings.append(last + ("=" if last.startswith("--") else " ") + value)
        return ", ".join(spellings)


class ArgumentParser(argparse.ArgumentParser):
    """`argparse.ArgumentParser` that exits with this runner's usage status.

    `ArgumentParser.error` calls `sys.exit(2)` of its own, before any code
    here runs, so every malformed command line the parser rejects took the
    exit status argparse chose rather than the one this runner documents. On
    the scale in `EXIT_OK` and its siblings, 2 is `EXIT_RECOVERED`, so
    `./run-tests --nonsense` reported a run in which every suite passed and
    the device had to be recovered.

    `exit` is overridden as well as `error`, because `--help` and `--version`
    reach `exit` directly and a successful one of those must still be 0.
    """

    def exit(self, status: int = 0, message: str | None = None) -> None:
        if message:
            self._print_message(message, sys.stderr)
        raise SystemExit(EXIT_USAGE if status else 0)

    def error(self, message: str) -> None:
        self.print_usage(sys.stderr)
        self.exit(EXIT_USAGE, f"{self.prog}: error: {message}\n")


def build_parser() -> argparse.ArgumentParser:
    parser = ArgumentParser(
        prog="run-tests",
        formatter_class=HelpFormatter,
        usage="run-tests [OPTIONS] [TARGET ...]",
        add_help=False,  # added below, so that it sits in a named section
        description=(
            "Run the hardware test suites against one or more Ultimate devices.\n"
            "\n"
            "Each suite is a separate process driving the firmware over REST, FTP or\n"
            "Telnet. The suites share one device, so the runner puts it back into a\n"
            "known state around each one. With no options the runner runs the E2E gate\n"
            "once, in overlay mode, against $U64_HOST.\n"
            "\n"
            "targets:\n"
            "  host                    a device that is also its own C64, as u64\n"
            "  cartridge@computer      a cartridge, in the computer that supplies its\n"
            "                          keyboard and video, as u2@c64u\n"
            "\n"
            "  Each target runs in a child process of its own, and every line of\n"
            "  output names the target it came from. Independent targets run at the\n"
            "  same time; targets sharing a machine take turns, so u64 and u2@c64u run\n"
            "  together and c64u and u2@c64u do not.\n"),
        epilog=(
            "examples:\n"
            "  run-tests                           the E2E gate, overlay mode\n"
            "  run-tests u2@c64u                   a cartridge, in its computer\n"
            "  run-tests u64 u2@c64u               both, at the same time\n"
            "  run-tests -s prg-context-menu       one suite\n"
            "  run-tests -m telnet,freeze          two UI transports\n"
            "  run-tests --list                    every registered suite\n"
            "\n"
            "  Keeping a run, and reading it afterwards:\n"
            "\n"
            "  run-tests -o runs/ u64              keep every artifact\n"
            "  run-tests -o runs/ --syslog u64     and the device's own log\n"
            "  run-tests -o runs/ --record u64     and a video of the run\n"
            "  python3 tools/e2e_report.py runs/   write runs/index.md\n"
            "  less runs/index.md                  read it; no device needed\n"
            "\n"
            "  Less often:\n"
            "\n"
            "  Bring a wedged device back:\n"
            "    run-tests --recover-command 'build u64' -o runs/\n"
            "\n"
            "  Take the device log from a second interface:\n"
            "    U64_LOG_ADDRESSES='u64=192.0.2.71' run-tests -o runs/ --syslog u64\n"
            "\n"
            "  Watch a recording, subtitled with the check that was running:\n"
            "    mpv --sub-file=runs/u64/video.srt --scale=nearest runs/u64/video.mp4\n"
            "\n"
            "exit status:\n"
            "  A severity scale, so it may be compared with an ordering\n"
            "  operator: `[ $? -le 2 ]` tolerates a retry and a recovery and\n"
            "  nothing worse.\n"
            f"  {EXIT_OK}  every suite passed first time, and nothing needed recovering\n"
            f"  {EXIT_RETRIED}  every suite passed, but at least one needed more than one attempt\n"
            f"  {EXIT_RECOVERED}  every suite passed, but a device needed recovering\n"
            f"  {EXIT_SUITE_FAILED}  a suite failed every attempt it was given\n"
            f"  {EXIT_DEVICE_UNHEALTHY}  a device could not be made healthy; the run was abandoned\n"
            f"  {EXIT_USAGE}  the command line was invalid. Not on the scale: a usage\n"
            "      error is not an outcome of a run. The value is EX_USAGE from\n"
            "      sysexits.h.\n"
            "\n"
            "notes:\n"
            "  The devices must be otherwise idle. The suites open the menu, reset the\n"
            "  machine, mount images, and put the settings back afterwards.\n"
            "\n"
            "  Suite order is fixed and significant. The registry at the top of this\n"
            "  file sets it.\n"
            "\n"
            "see also:\n"
            "  tests/README.md         categories, environment, exit status\n"
            "  tests/e2e/README.md     adding or changing a suite\n"
            "  tests/lib/README.md     the JSONL record shapes\n"
            "  tests/e2e/doc/          what a run records, and why\n"))

    arguments = parser.add_argument_group("arguments")
    arguments.add_argument("targets", nargs="*", metavar="TARGET",
                           help="Name a host, or cartridge@computer, in the "
                                "forms listed under targets. May also be "
                                "given with -H.")

    general = parser.add_argument_group("general")
    general.add_argument("-h", "--help", action="help",
                         help="Show this help and exit.")

    selection = parser.add_argument_group(
        "run selection",
        "Which categories run. With none of these, the E2E gate runs.")
    selection.add_argument("--e2e", action="store_true",
                      help="Run the functional and regression suites. They "
                           "are the release gate, and the default.")
    selection.add_argument("--perf", action="store_true",
                      help="Run the benchmarks. They report a measurement, "
                           "not a verdict.")
    selection.add_argument("--soak", action="store_true",
                      help="Run the duration and load tests: leaks, "
                           "exhaustion, transport degradation.")
    selection.add_argument("--all", action="store_true",
                      help="Run all three categories, in the order e2e, "
                           "perf, soak.")
    selection.add_argument("-l", "--list", action="store_true",
                      help="List every registered suite with its category "
                           "and path, then exit.")
    selection.add_argument("--list-profiles", nargs="?", const="", metavar="NAMES",
                      help="Show which suites each profile selects, as a "
                           "matrix, then exit. Give a comma-separated list of "
                           "profile names to compare only those. Scenario and "
                           "check counts and durations are not shown, because "
                           "the registry does not know them; add --measured "
                           "to fold in what recorded runs did.")
    selection.add_argument("--measured", action="append", metavar="PROFILE=DIR",
                      default=[],
                      help="A profile name and a `-o` output directory that "
                           "profile produced. Repeatable. Adds a table per "
                           "profile of what each suite actually ran on each "
                           "machine, and how long it took.")
    selection.add_argument("--format", choices=("text", "json", "markdown"),
                      default="text",
                      help="How --list-profiles writes its answer: aligned "
                           "text, JSON for a script, or Markdown tables "
                           "(default: text).")

    e2e = parser.add_argument_group(
        "e2e options",
        "Which suites run, and which UI transport drives them.")
    e2e.add_argument("-s", "--suite", action="append", default=[], metavar="NAME",
                     help="Run only this suite. Repeatable, and it selects "
                          "manual suites as well. The names are in --list.")
    e2e.add_argument("-m", "--mode", "--modes", dest="modes",
                     default="", metavar="LIST",
                     help="Drive the suites through this UI transport: "
                          "overlay, freeze, telnet, a comma-separated list, "
                          "or all. Each transport is a full pass over the "
                          "selected suites. Defaults to what the profile "
                          "sweeps, which is overlay up to standard and all "
                          "three from deep.")
    e2e.add_argument("--manual", action="store_true",
                     help="Also run the suites kept out of the default run: "
                          "those needing an operator, root, or a long wait.")
    e2e.add_argument("--assume-fix", action="append", default=[], metavar="NAME",
                     help="Run the checks this machine normally skips for "
                          "want of a firmware fix, which is how a backport "
                          "is found. Repeatable, and 'all' assumes every "
                          "fix. The names are in tests/lib/machine.py.")
    e2e.add_argument("--validate-openapi", action="store_true",
                     help="Check every REST answer against the generated "
                          "OpenAPI document in doc/api, for every suite. The "
                          "openapi-contract suite does this for itself either "
                          "way; this extends it to the whole run.")
    e2e.add_argument("--profile", choices=list(profiles.ORDER),
                     default=profiles.DEFAULT,
                     help="How much of the tree to cover, shallowest first: "
                          + ", ".join(profiles.ORDER)
                          + f" (default: {profiles.DEFAULT}). A profile is a "
                            "named bundle: which suites and scenarios run, "
                            "which UI transports are swept, and whether the "
                            "suites an ordinary run leaves out are included. "
                            "-s names a suite whatever the profile says, and "
                            "-m overrides the transports it would sweep.")
    e2e.add_argument("--kernal", default="", metavar="IMAGE",
                     help="Run the Software IEC suites (rel-copy, "
                          "iec-dos-commands, softiec-soak) under this KERNAL: a "
                          "file name in the device's ROM directory, or a local "
                          "file uploaded there for the run (default: the "
                          "device's own).")
    e2e.add_argument("--command-interface", action="store_true",
                     help="Enable the Command Interface for the Software IEC "
                          "suites, which the UCI (hyperspeed) KERNAL needs to "
                          "reach the drive.")
    e2e.add_argument("--soak-duration", "--soak-profile", dest="soak_profile",
                     choices=("stress", "soak"), default="stress",
                     help="Set how long the network soak runs: stress takes "
                          "about two minutes, soak twelve hours (default: "
                          "stress).")

    device = parser.add_argument_group(
        "device",
        "Where the device is, and how long to wait for it.")
    device.add_argument("-H", "--host", metavar="TARGET", default=DEFAULT_HOST,
                        help="Name the target, when it is not given as a "
                             "positional (default: $U64_HOST, or u64).")
    device.add_argument("-p", "--password", metavar="PASSWORD",
                        default=DEFAULT_PASSWORD,
                        help="Authenticate REST and FTP with this password "
                             "(default: $U64_PASS, empty).")
    device.add_argument("-t", "--timeout", metavar="SECONDS", default=DEFAULT_TIMEOUT,
                        help="Wait this long for one REST request to answer, "
                             f"in every suite (default: {DEFAULT_TIMEOUT}).")

    health = parser.add_argument_group(
        "health and recovery",
        "Before each E2E suite, and again after one fails, the device must be\n"
        "reachable, drivable and functional. A suite that fails on a healthy device\n"
        "found something; one that fails on an unhealthy device showed nothing, so\n"
        "the device is recovered and the suite runs again. A run that needed a\n"
        "recovery exits 3, not 0.")
    health.add_argument("--recover-command", default="", metavar="CMD",
                         help="Run this shell command to bring an unhealthy "
                              "device back, for example 'build u64'. There is "
                              "no default: the mechanism is site-specific, so "
                              "without it nothing is recovered. @HOST@, @PASS@ "
                              "and @TIMEOUT@ are filled in with the target "
                              "this recovery is for, which is how one command "
                              "serves a run naming several targets. Its exit "
                              "status is advisory; the health check decides "
                              "whether the device came back."),
    health.add_argument("--recover-max-per-suite", type=int,
                         default=DEFAULT_RECOVER_MAX_PER_SUITE, metavar="N",
                         help="Allow one suite this many recoveries "
                              f"(default: {DEFAULT_RECOVER_MAX_PER_SUITE}). "
                              "How many times a suite is run is --attempts.")
    health.add_argument("--recover-max-total", type=int,
                         default=DEFAULT_RECOVER_MAX_TOTAL, metavar="N",
                         help="Allow the whole run this many recoveries; "
                              "exhausting them abandons the run (default: "
                              f"{DEFAULT_RECOVER_MAX_TOTAL}).")
    health.add_argument("--recover-timeout", type=float,
                         default=DEFAULT_RECOVER_TIMEOUT_SECONDS, metavar="SECONDS",
                         help="Kill the recovery command after this long "
                              f"(default: {DEFAULT_RECOVER_TIMEOUT_SECONDS:g}).")
    health.add_argument("--no-restore-settings", dest="restore_settings",
                         action="store_false", default=True,
                         help="Leave the device's settings however the run "
                              "left them. By default every setting each "
                              "machine serves is read before the first suite "
                              "and the ones that differ are written back "
                              "afterwards, so the next run starts from the "
                              "same configuration this one did. The read is "
                              "15 to 23 requests per machine.")
    health.add_argument("--no-health-check", dest="health_check",
                         action="store_false", default=True,
                         help="Reduce health to reachable and drivable. Use "
                              "it when a listener is off on this device by "
                              "design; the full sweep costs about 150ms per "
                              "suite.")
    health.add_argument("--attempts", type=int, default=DEFAULT_ATTEMPTS,
                         metavar="N",
                         help="How many times a suite may run in total, "
                              "counting the first. The flag counts "
                              "executions, not retries, so --attempts 3 runs "
                              "a failing suite three times and 3 is the "
                              "highest `attempt` any record can carry "
                              f"(default: {DEFAULT_ATTEMPTS}).")
    health.add_argument("--no-retry", dest="attempts", action="store_const",
                         const=1,
                         help="Run every suite exactly once. An alias for "
                              "--attempts 1.")

    artifacts = parser.add_argument_group(
        "run artifacts",
        "What the run keeps for reading afterwards. tools/e2e_report.py turns an\n"
        "output directory into runs/index.md, which needs no device: a status line,\n"
        "then every failing check with the screen, the log tail and the command that\n"
        "runs it again.")
    artifacts.add_argument("-o", "--output-dir", default="", metavar="DIR",
                        help="Keep the whole run under DIR, one directory per "
                             "target: every check, suite, health sweep and "
                             "device request as JSONL, each suite's console "
                             "log, and the screens it read. The record shapes "
                             "are in tests/lib/README.md.")
    artifacts.add_argument("--syslog", action="store_true",
                        help="Collect the devices' own log into DIR, and check "
                             "at both ends of the run that each is still "
                             "configured to send it. Needs Network Settings / "
                             "Log to Syslog Server pointed at this host, which "
                             "takes effect when the device next boots.")
    artifacts.add_argument("--syslog-port", type=int,
                        default=syslog_collector.DEFAULT_PORT, metavar="PORT",
                        help="Collect the device log on this port (default: "
                             f"{syslog_collector.DEFAULT_PORT}). Not 514, "
                             "which needs root.")
    artifacts.add_argument("--no-screens", dest="screens", action="store_false",
                        default=True,
                        help="Stop keeping the screens the suites read. They "
                             "cost the device nothing, since the suites fetch "
                             "them anyway, and they are the only record of "
                             "what was on screen when a check failed.")

    recording = parser.add_argument_group(
        "recording",
        "A video of the run. Off by default: it costs the device two streams and\n"
        "the LAN their bandwidth for the whole run.")
    recording.add_argument("--record", action="store_true",
                           help="Record the harness's screen beside the "
                                "device's video, with the device's audio, one "
                                "file per target. Needs ffmpeg with "
                                "libx264rgb.")
    recording.add_argument("--no-record-video", dest="record_video",
                           action="store_false", default=True,
                           help="Drop the device's video and its stream.")
    recording.add_argument("--no-record-audio", dest="record_audio",
                           action="store_false", default=True,
                           help="Drop the audio track and its stream.")
    recording.add_argument("--no-record-menu", dest="record_menu",
                           action="store_false", default=True,
                           help="Drop the harness pane. The screens are still "
                                "kept under -o.")
    recording.add_argument("--record-menu-min-interval-ms", type=int, default=0,
                           metavar="MS",
                           help="Leave at least this long between two "
                                "menu_screen requests the recorder makes for "
                                "itself, which it does only while no suite is "
                                "reading one (default: 0). The device serves "
                                "about four HTTP connections.")
    recording.add_argument("--record-layout", choices=("combined", "separate"),
                           default="combined",
                           help="Write both panes into one file, or one file "
                                "each (default: combined). Separate files stay "
                                "frame aligned and carry the same audio.")
    recording.add_argument("--no-record-stamp", dest="record_stamp",
                           action="store_false", default=True,
                           help="Draw nothing into the frames: no timecode, "
                                "failure edge, progress bar or cards. The "
                                "device's pixels and nothing else.")
    recording.add_argument("--record-quality", default="lossless", metavar="LEVEL",
                           help="Encode at this quality: lossless, or a number "
                                "for a lossy encode (default: lossless). The "
                                "material is 40-column text, which a lossy "
                                "encode blurs.")
    recording.add_argument("--record-scale", type=int, default=1, metavar="N",
                           help="Upscale the canvas by this whole number, "
                                "nearest neighbour (default: 1). A fractional "
                                "scale resamples pixel art.")
    recording.add_argument("--record-fps", type=int, default=RECORD_FPS,
                           metavar="N",
                           help="Write this many frames a second (default: "
                                f"{RECORD_FPS}). Well below 50: a mostly "
                                "static screen shows a reader no more at fifty "
                                "frames a second than at ten.")
    recording.add_argument("--record-keyint", type=float,
                           default=RECORD_KEYINT_SECONDS, metavar="SECONDS",
                           help="Place a keyframe this often while the screen "
                                f"is static (default: {RECORD_KEYINT_SECONDS:g}). "
                                "Bounds how far a player decodes when stepping "
                                "backwards.")
    recording.add_argument("--record-ffmpeg-args", default="", metavar="ARGS",
                           help="Add these arguments to the encoder command. "
                                "They go last, so they override everything "
                                "above, including the pixel exactness the "
                                "lossless default is for.")

    console = parser.add_argument_group(
        "output and execution",
        "What the console shows, and when to stop.")
    report.add_colour_argument(console)
    console.add_argument("-x", "--stop-on-fail", action="store_true",
                        help="Stop at the first failing suite instead of "
                             "running the rest.")

    return parser


def resolve_modes(raw: str, profile: str = profiles.DEFAULT) -> list[str]:
    """The transports to sweep: what -m named, or what the profile sweeps."""
    if raw.strip() == "all":
        return list(MODES)
    modes = [m.strip() for m in raw.split(",") if m.strip()]
    for mode in modes:
        if mode not in MODES:
            die(f"unknown mode: {mode} (expected {', '.join(MODES)} or all)")
    return modes or list(profiles.modes_for(profile))


def apply_colour_choice(choice: str) -> None:
    """Honour --color here and in every suite this process starts.

    One implementation for every program in this tree, in report.py: the
    suites are child processes, so the choice is exported rather than only
    applied locally, and NO_COLOR and FORCE_COLOR are the names report.py
    reads. Under `auto` the suites share this stdout and reach the same
    answer on their own.
    """
    report.apply_colour(choice)


def resolve_targets(args: argparse.Namespace) -> list[targets_lib.Target]:
    """Every target this run was asked for, validated before anything starts.

    Two tokens that resolve to the same pair of machines are one target. With
    `U64_COMPUTERS=u2@c64u` set, `u2` and `u2@c64u` are two spellings of the
    same cartridge in the same computer, and running both would run every
    suite twice against the same hardware while the scheduler kept them apart.
    The first spelling is the one kept, and the run says which one it dropped.
    """
    tokens = list(args.targets) or [args.host]
    for cartridge, computer in cabling.establish(tokens, args.password, args.timeout):
        report.detail(f"cabling: {cartridge} is plugged into {computer}")
    resolved: list[targets_lib.Target] = []
    for token in tokens:
        try:
            target = targets_lib.parse(token)
        except targets_lib.TargetError as exc:
            die(str(exc))
        if any(target.token == other.token for other in resolved):
            die(f"{target.token} is named twice")
        same = next((other for other in resolved
                     if (other.device, other.computer) == (target.device, target.computer)),
                    None)
        if same is not None:
            report.warn(f"{target.token} names the same machines as "
                        f"{same.token}; running it once")
            continue
        resolved.append(target)
    return resolved
