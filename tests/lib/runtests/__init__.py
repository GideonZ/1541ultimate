"""The machinery behind the repository-root `run-tests` runner.

`run-tests` keeps the suite registry and the orchestration that runs it. The
modules here hold what that orchestration calls, one concern per module.
Imports run from the leaves up: `constants`, `model` and `exits` depend on
nothing else here, `device` and `identity` on those, and every other module on
some of them.
"""
