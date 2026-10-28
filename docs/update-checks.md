# Sprintathon update checks

The main-menu button starts a background check or opens the newer release page.
Automatic checks run at most once per day. Turn them off in Preferences →
Sprintathon → Check for Sprintathon Updates. Manual checks remain available.
Results are cached per build. Failed checks can be retried from the menu.
Nothing is downloaded or installed automatically.

The checker uses GitHub's latest published, non-prerelease release. It compares
that tag against the embedded Git commit. Only an `ahead` result is advertised
as an update; identical/older releases are not. A diverged or unidentified build
shows “Cannot compare this build” and links to the release list instead.
Draft releases are never offered. Requests verify HTTPS, time out, and are
cancelled when the application exits. Builds require the existing curl support
for online checking.

## Building

The Sprintathon draft-release workflow passes its tag into the shared binary
workflow. Windows, macOS and Linux stamp their source trees before compiling.
The generated `Source_Files/Misc/SprintathonBuildVersion.h` contains the release
tag and exact checked-out commit. Rebuilds without a tag are development builds.

For local builds, run this after updating your checkout and before compiling:

```sh
python3 .github/scripts/stamp-sprintathon-build.py
make -j"$(nproc)"
```

The Git identity describes the committed base; it cannot identify uncommitted
local edits. A build made without stamping has an unknown identity and will not
claim that another release is newer. The default header intentionally contains
no hard-coded release number.

## Validation

Tested with a mocked transport: newer/identical/older/diverged commits, missing
releases, draft/prerelease rejection, malformed responses, HTTP failures,
manual release links, daily caching, disabled automatic checks, and cancellation.
The transport compiles against curl; response-size limits and URL encoding were
checked separately. The JSON dependency in these isolated tests was a test double;
production uses the engine's existing Boost dependency. Build stamping and
workflow YAML parsing were tested. Full platform builds, real HTTPS responses,
and the in-game menu layout still require validation.
