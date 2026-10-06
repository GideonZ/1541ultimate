# Contributing to the 1541 Ultimate firmware

Thanks for wanting to help! This repository holds the firmware for the
Ultimate-II, Ultimate-II+ and Ultimate-II+L cartridges, the Ultimate 64, the
Ultimate 64 Elite II and the C64 Ultimate. Experiments and new ideas are
always welcome.

This page explains how to get your work merged smoothly. The short version:

1. **Small fixes:** open a pull request against `master`.
2. **Anything bigger:** open an issue first and agree the plan.
3. **Show that it works:** say what you tested, and on which hardware.

## Small fixes: just open a pull request

Bug fixes, small improvements, new tests and documentation fixes don't need
any discussion first. If there's an issue for it, link it.

Not sure whether your change counts as small? Open an issue and ask. It's
quick.

## Bigger changes: open an issue first

Open an issue, or comment on an existing one, **before** you write most of the
code if your change does any of these:

- adds a feature, a setting, a menu entry, a REST endpoint, a UCI command or
  anything else other people will build on
- changes how an existing feature behaves, beyond fixing a bug
- changes the overall architecture, restructures existing code, or changes a
  class that many parts of the firmware share
- touches FPGA code under `fpga/`, since the maintainer builds and checks the
  FPGA images
- is likely to grow beyond about 300 lines, not counting tests

In the issue, describe the problem you want to solve, which products it
affects, and roughly how you plan to solve it. A few paragraphs is plenty.
Once the maintainer, or the area contact listed below, has replied that they're
happy with the approach, go ahead. This applies to issues you open yourself
too: opening the issue starts the conversation, the reply is the go-ahead.

You're free to keep exploring in your fork while the discussion goes on. If a
prototype helps explain the idea, open it as a draft pull request and link it
from the issue. Prototypes often change quite a bit on the way to being
merged.

## Keeping work easy to merge

- **One problem per issue, one issue per pull request.** If you find
  something else along the way, open a new issue for it and link the two,
  rather than adding it to the one you're working on.
- **Keep the scope you agreed.** Once a pull request is under review, only
  change it to answer the review. New ideas go into a new issue.
- **Each pull request stands on its own.** It should apply to current
  `master`, build, and be useful without any other open pull request. If the
  work needs several steps, open the next one after the previous one is
  merged, rather than a chain that has to be merged in a set order.
- **Open it when it can be merged.** If your change needs something that
  isn't on `master` yet, such as an FPGA change or a decision in another
  issue, say so in the issue and keep your work in your fork, or in a draft
  pull request, until that has landed.
- **Have only a few open at once.** Finishing one pull request before starting
  the next gets all of them merged sooner.

## Who to ask about which area

Changes to the overall architecture, and all FPGA (VHDL) code, go through the
maintainer, @GideonZ.

The people below know particular areas well and are happy to help. If your
change touches one of their areas, mention them in your issue or pull request,
and agree the plan with them before you start on anything bigger than a small
fix. Each person wrote much of the code in their area or has worked on it
recently. Areas are in alphabetical order.

| Area | Main paths | Contact |
| --- | --- | --- |
| Command interface (UCI), GitHub Actions workflows | `software/io/command_interface`, `.github/workflows` | @barryw |
| Ethernet drivers, G64 and G71 track parsing | `software/io/network`, `software/drive` | @enver-haase |
| Hyperspeed Kernal | `roms/c64rom/kernal` | @bvl1999 |
| Machine code monitor, REST API, test suites, USB keyboard and mouse | `software/monitor`, `software/api`, `tests/`, `software/io/usb` | @chrisgleissner |
| Modem emulation | `software/io/acia` | @xlar54 |
| Printer emulation | `software/io/printer` | @rgc2000 |
| SID player | `software/6502/sidcrt` | @WilfredC64 |
| Software IEC drive and DOS commands | `software/io/iec` | @markusC64, @chrisgleissner |
| Web UI | `html/` | @TangoBravo64, @radius75 |

For every other area, the maintainer is your contact. Just open the issue.

Want to be listed, or taken off the list? Open a pull request.

## Fitting in with the code

- **Match the code around you.** Follow the style of the file you're editing:
  usually 4-space indentation, function braces on their own line and other
  braces at the end of the line. Don't reformat, rename or move code your
  change doesn't need.
- **Use Linux (LF) line endings.** Some older files still have Windows (CRLF)
  line endings. They will be converted all at once later, so when you edit
  one, keep its existing endings. If `git diff --stat` shows a whole file
  changed, something converted it.
- **Keep shared code generic.** Classes used across the firmware, such as the
  file systems, the file manager, the browser and the menus, should not know
  about one particular feature. If a feature needs special behaviour, give it
  its own type, for example a subclass. If shared code has a bug, fix it there
  rather than working around it.
- **Think about every product.** Shared code is built for all of them. The
  `U64` macro is 1 on the Ultimate 64, 2 on the Ultimate 64 Elite II and the
  C64 Ultimate, and undefined on the cartridges, so `#if U64 == 1` leaves out
  two products. Where a feature makes sense on every product, try to make it
  work on every product.
- **Mind the size.** The Ultimate-II has little room left, and CI checks every
  image. String formatting comes from `software/system/small_printf.cc`, which
  has no `l`, `ll` or `z` length modifiers. New C library calls can pull in
  large parts of the library and break the RISC-V builds.
- **Comments say why.** Describe the code as it is, not how it got there. Git
  keeps the history.

## Testing

Building instructions are in [README.txt](README.txt). CI builds the firmware
for every product on each pull request, and the build must pass. On your first
pull request, a maintainer may need to approve the CI run before it starts.
You can download the update files from the run's artifacts and try them on
your own device.

Beyond that:

- **Work red/green.** Write the test first and run it against firmware
  without your change, where it should fail. Then run it with your change,
  where it should pass. For a new feature, the test shows the feature
  working.
- **Reuse what's there.** Hardware tests live under `tests/` and run through
  `./run-tests`. Build on the shared helpers in `tests/lib` rather than
  writing your own, and make sure your suite runs well alongside the others
  and leaves the device as it found it. Logic that runs on a PC without much
  mocking can also get a host unit test next to its code, such as
  `software/api/tests/`. See [tests/README.md](tests/README.md).
- **Run the standard profile.** `./run-tests --profile standard -o runs/ u64`
  runs the same set of suites a merge is checked against. Run it red and
  green, and `python3 tools/e2e_report.py runs/` writes a report you can
  attach.
- **Test on both device types.** We expect a test on at least an Ultimate 64
  or C64 Ultimate and on an Ultimate-II cartridge, since they differ in many
  ways. If you could not test one of them, say so.
- **Check the device runs your build.** `GET /v1/info` shows the commit in
  `git_commit_hash`. If a check fails unexpectedly, make sure the check itself
  is right before you change the firmware.

## Writing the pull request

- **Use GitHub's draft status** while the work isn't ready for review.
- **Commit only source.** Builds leave firmware images and object files in the
  working tree, so add files by name rather than with `git add -A`.
- **Keep the description short.** It needs four things:
  - the problem, with a link to the issue (`Fixes #123`)
  - what you changed, and why you chose this approach
  - how you tested it: the devices with their firmware and FPGA versions,
    the host you ran from, the `./run-tests` results red and green, and any
    manual steps
  - anything you know is missing or untested

## Using AI tools

AI tools are welcome. You are still the author, so make sure you understand
the change and can explain it in review. A few habits help:

- Use the most capable model you have access to. At the time of writing
  (October 2026) that is Claude Opus 5.5 with high reasoning effort.
- Before opening the pull request, ask a second AI session or a person to
  review the change critically, and fix what they find.
- Check what the tool tells you against the code.
- Mention the model you used in the description.

## Working together

Everyone here works on this firmware for the love of it, mostly in their spare
time. A few things keep discussions friendly and useful:

- **Be kind and assume good intent.** A short reply usually means a busy
  person, not an unhappy one.
- **Discuss the idea, not the person.** When you disagree, say why with
  something concrete, such as a test result, a measurement or a line of code,
  and suggest an alternative.
- **Keep it in the issue or pull request,** so others can follow and join in.
- **When you review, help the change land.** Mention what works as well as
  what doesn't, and offer a fix or a test where you can.
- **Build on decisions once they're made.** If you see new evidence, raise it
  in the issue. The maintainer makes the final call on direction.

## After you open the pull request

Pull requests are reviewed by hand. The maintainer may squash your commits or
adjust the change while merging it. If an issue or pull request has gone quiet
for a couple of weeks, a friendly reminder is welcome.

## Reporting bugs and asking for features

When you report a bug, include the product, the firmware version, the steps to
reproduce it, and what you expected to happen. Feature ideas are welcome as
issues too. Describe the need, not only the solution you have in mind.

The user manual lives in its own repository,
[GideonZ/1541u-documentation](https://github.com/GideonZ/1541u-documentation).
Corrections to the manual belong there.

## License

This repository is licensed under the GNU General Public License v3, see
[LICENSE.txt](LICENSE.txt). Contributions are accepted under the same terms.
