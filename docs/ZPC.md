# Experimental ZPC exploration mode

Launch Sprintathon directly with an original, unpacked ZPC data directory:

```sh
./sprintathon --zpc --zpc-support "$HOME/Downloads/ZPC-Preview" "$HOME/Games/ZPC"
```

The support directory can be the earlier ZPC-Preview package or an unpacked
Marathon 2 data set. It must contain `Shapes`, `Images`, and `Sounds`, or their
`.shpA`, `.imgA`, and `.sndA` equivalents. Python is not required at runtime.

The ZPC directory must contain:

- `ZPCshapes.shpA` (also accepts `zpcshapes.shpA`, `Shapes`, `Shapes.shpA`)
- `zpcmap.sceA` (also accepts `ZPCmap.sceA`, `Map`, `Map.sceA`)
- `landscap.lnd` (also accepts `LANDSCAP.LND`)

Unzip the Map and Shapes attachments first. The original files are never
modified. The importer generates a separate scenario inside the application's
local data directory under `ZPC-import-v2/<content fingerprint>`. It prints the
path at launch. Identical generated files are reused; missing files are rebuilt.
Imports fail with a ZPC-specific explanation when required inputs or supported
format checks fail. Without `--zpc`, normal scenario loading is unchanged.

## What this implements

This is an engine-integrated importer, not a complete ZPC engine emulation.
It reads the original ZPC files at launch and translates their geometry and
textures into a Marathon-compatible exploration scenario. It does not widen
Marathon's packed shape descriptor or change Marathon's collection assignments.

All 36 levels retain their geometry and wall artwork. Eight 900 x 400 indexed
landscapes are read from `landscap.lnd`, resampled with nearest-neighbor sampling
to 1024 x 512 and packed as eight frames of collection 27. Assignment in file
order to original landscape collections 23–30 is inferred, not verified against
an original ZPC playthrough. Their palette comes from the original Shapes file.

Console commands: `.1` through `.36` select a level, `.next` advances, and `.open`
opens platforms. OpenGL rendering is recommended. The player has replenished
health and oxygen and hidden weapons. The Lua weapons setter correction is
required; the distribution bundle includes it as a separate patch so an already
applied correction need not be reapplied.

ZPC monsters and scenery are restored as static, non-solid displays using original
placements, palettes, colour variants and viewing directions. Dormant actors are
made visible. Combat, AI, items, liquids, terminals, control panels, automatic exits,
and game-specific sounds/physics remain disabled. Start a new game after importing;
old exploration saves do not include these displays. The supplied executable was
inspected to recover the display mappings; it is never executed by the importer.
The player, interface, and fallback sounds still come from Marathon support data.
Treat saves and films from this experimental mode as disposable. Do not use the
mode for multiplayer compatibility testing.

## Validation

The importer was compiled and run with AddressSanitizer and UndefinedBehaviorSanitizer
against the supplied ZPC files. The generated Shapes passed an isolated harness
using Sprintathon's native shape decoding functions: 36 collection variants,
1,769 bitmaps, and 430 sequences. All 2,098 monster and 990 scenery
placements in the supplied maps were checked against their original coordinates. All 43,396 nonempty surface references were
checked, and original line/endpoint geometry was compared byte for byte.
Truncated data, bad WAD offsets/chunk links, wrong landscape sizes and unsupported
surface IDs are covered by the standalone regression test. Full engine compilation
and in-game validation of this new mode remain to be done on a machine with the
build dependencies installed.

To run the standalone importer regression test from the source root:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/zpc_import_test.cpp -o /tmp/zpc-import-test
/tmp/zpc-import-test /path/to/ZPCshapes.shpA /path/to/zpcmap.sceA \
  /path/to/marathon2/Shapes.shpA /path/to/landscap.lnd \
  /tmp/zpc-test-Shapes /tmp/zpc-test-Map
```

The last two paths are test output files and will be overwritten.
