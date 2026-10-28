# Experimental Myth II terrain import

This package contains a converted test level made from your `displacement.obj`
and `color.bmp`, plus an engine patch and a reusable Python importer.

## Apply and build

Requires the earlier experimental terrain rendering, player/monster/projectile
collision, flashlight-shadow and rocket-crater patches. This is an incremental
patch against that source, including the `shell.h` compile fix.

Extract this archive into Downloads, then:

```bash
cd ~/Projects/alephone &&
git apply --check ~/Downloads/sprintathon-myth-terrain-import/sprintathon-myth-terrain-import.patch &&
git apply ~/Downloads/sprintathon-myth-terrain-import/sprintathon-myth-terrain-import.patch &&
make -j"$(nproc)"
```

## Play the test

Use Marathon Infinity data and the OpenGL shader renderer:

```bash
bash ~/Downloads/sprintathon-myth-terrain-import/myth-test/launch.sh \
  "$HOME/Projects/alephone/Source_Files/sprintathon" \
  "$HOME/Games/mi"
```

Press **New Game**, select **Myth terrain import**, then start. If your saved map
selection takes precedence, select the supplied `myth-test/Myth Terrain.sceA`
in the level selector. Keep the launcher environment active; launching the map
without it renders the ordinary flat host polygons.

For this first test, disable Horror mode and use ordinary sector lighting so the
terrain colour map is easy to inspect. The optional batch renderer can remain on;
terrain bypasses its retained flat-floor batches.

The launcher sets these variables only for this process:

- `SPRINTATHON_TERRAIN_MESH`: converted, Z-up `terrain.obj`.
- `SPRINTATHON_TERRAIN_COLOR`: the BMP colour map.
- `SPRINTATHON_TERRAIN_WORLD=1`: absolute placement across connected host polygons.

## What was imported

- All 25,921 original vertices and 51,200 source triangles, with original UVs.
- The 1280 × 1280 colour map, uploaded once and mipmapped as a dedicated material.
- A 48 × 48 world-unit level, divided into 36 connected host polygons.
- A uniform scale of 0.3, with axes changed from `(X,Y,Z)` to `(X,-Z,Y)`.
- Terrain raised so its minimum is 0.75 world units above the host floor; maximum
  height is about 3.94 world units. No edge flattening between sections.
- A player start near the centre, a daylight landscape ceiling and solid outer
  boundaries rendered as sky.

Clipping against the host polygons produces 56,288 render triangles. This adds
vertices along portal boundaries without changing the source terrain surface.
Player/monster support, projectile intersections, flashlight shadow casting and
rocket dents use these same section meshes. Craters crossing section boundaries
update both sides. The test level contains no enemies or weapon pickups.

This is terrain-only. The rivers are painted colour, not Marathon liquid; there
are no imported units, scenery objects, bridges, sounds, passability rules or
mission scripts. The colour map may already contain painted lighting/shadows.
`preview.png` is an offline textured-mesh preview, not an in-game screenshot.

## Import another export

From the patched repository (Python 3, no extra Python packages):

```bash
python3 tools/import_myth_terrain.py \
  /path/to/displacement.obj /path/to/color.bmp \
  /path/to/my-new-test --width 48
```

The standalone `import_myth_terrain.py` included beside this README works too.
Use a triangulated, Y-up Myth2ools export with positive vertex/UV indices and a
BMP colour map. It does not read Myth archives or mesh tags directly. Width is
limited to 8–48 world units; especially tall maps may require a smaller width.
The generated launcher takes the engine executable and scenario-data directory.

## Validation and limits

Verified here:

- WAD header, checksum, chunk lengths, polygon winding, boundary sides, shared
  portal ownership, full grid connectivity and player-start references.
- C++ terrain parsing, complete area coverage, original height preservation,
  collision at every clipped triangle centroid and continuous section heights.
- Projectile intersection, seam-crossing craters, level reset and network opt-out.
- The older single-polygon terrain and crater regression checks.
- BMP row orientation, mip upload caching and GL unpack-state restoration using
  mock SDL/OpenGL calls.

A full engine build and an in-game run have **not** been performed here.
The terrain remains an experimental Sprintathon extension, not a stock Aleph One
map feature. Multiplayer/replay terrain is disabled. Craters are reset on level
load and are not saved. Monster navigation still uses the host polygon grid;
it does not yet have terrain-slope-aware pathfinding. Baked flat-floor lighting
is disabled on terrain. Test with the OpenGL shader renderer.
