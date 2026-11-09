# Sprintathon stationary benchmark

From the repository root, build with `make -j4`, then run for example:

```sh
DRI_PRIME=pci-0000_00_02_0 \
SPRINTATHON_RENDER_TRACE=1 SPRINTATHON_SETUP_TRACE=1 \
SPRINTATHON_DETAIL_GPU=0 SPRINTATHON_PRESENT_TRACE=1 \
python3 tools/run-sprintathon-benchmark.py --scenario "$HOME/Games/ax" \
  --level 3 --resolution 1280x720 --render-scale 100 --vsync off --fps-cap 0
```

The runner defaults to one run. `--runs N` repeats sequentially. Results are written to a new directory under `~/sprintathon-benchmarks` or `--output` outside this checkout. Leave the game focused and controls untouched. The fixed seed and stationary input do **not** make this a deterministic replay; AI, scripts, and frame-dependent behavior can vary.

Settings overrides are process-local and preference writes are suppressed for the whole benchmark process. An explicit resolution requests a window of that size; use `--fullscreen on` to request fullscreen instead, and record the actual framebuffer and render dimensions before comparing with fullscreen runs. `--batch`, `--texture-arrays`, `--hud`, `--soft-sector-edges`, `--texture-light-sources`, `--per-pixel-lighting` and `--baked-texture-lighting` accept `on` or `off`. `--texture-light-sources` changes the actual bright-texture source switch, which also affects baked lighting; dynamic texture lighting additionally requires per-pixel lighting, nonzero texture-light intensity and a nonzero light limit. Colored-light intensity separately controls projectile and other colored lights; Low sets it to zero. Use `--colored-light-intensity 0..200`, `--texture-light-intensity 0..200` and `--texture-light-limit 0..20` to set the other actual controls. These effective preferences are recorded. `--map` overrides the scenario's default campaign map; `--level` is one-based and is loaded directly as map index `level - 1`.

`--preset low` (or `medium`, `high`, `total`) applies the same preset callback as the preferences UI in a short preparation process, saving a private temporary settings profile. The measured process loads it before creating the window and rendering resources, then applies explicit overrides. The temporary profile is deleted after the run. An initialization window may appear briefly during preparation.

**Current limitations:** There is no independent dynamic-only texture-light switch: the texture source control is shared with baked lighting. The framebuffer dimensions are actual; the allocated render-target dimensions are logged separately from the `estimated_render` dimensions calculated from the active view and render scale. The application logs actual OpenGL renderer, version and selected campaign map. A supported configuration may still fall back at runtime; verify any such messages in the log.

`summary.json` reports frame intervals, exclusive CPU wall stages and sampled setup scopes. The presentation CSV contains asynchronous GPU timestamp intervals and CPU wall times for the frame IDs that originated them. Delayed query results are accepted only for frames originating in the measurement window; a few late results may remain unread at shutdown. Missing or unsupported GPU timing is reported explicitly. GPU timestamp intervals cover world, final scene, overlays, HUD and tail preparation, not the swap or an independently measured entire frame; they can include GPU idle/submission gaps. CPU and GPU times overlap and must not be added. Swap is measured separately on the CPU. Visibility covers tree construction, sorting and object placement; HUD CPU covers the primary HUD draw, while presentation timing also includes other overlays. Sampling and diagnostics add overhead; compare runs with identical trace settings and use no detailed diagnostics for final FPS. No query waits or `glFinish` are used.

The runner prints the end of the application log on startup failure, rejects incomplete runs, and keeps individual run summaries rather than hiding variation. An interrupted process can leave a partial CSV; only a `complete` status is valid.

## Other automated runs

For other tests, set `SPRINTATHON_TRANSIENT_SETTINGS=1` to disable preference
writes and apply process-local overrides before window creation without starting
the automatic benchmark. Numeric environment variable suffixes are
`WIDTH`, `HEIGHT` (set both), `FULLSCREEN`, `RENDER_SCALE`, `FPS_CAP`,
`VSYNC`, `BATCH`, `ARRAYS`, `HUD`, `SOFT_EDGES`, `TEXTURE_LIGHT_SOURCES`,
`PER_PIXEL_LIGHTING`, `BAKED_TEXTURE_LIGHTING`, `COLORED_LIGHT_INTENSITY`,
`TEXTURE_LIGHT_INTENSITY` and
`TEXTURE_LIGHT_LIMIT`;
prefix each with `SPRINTATHON_SETTING_`.
Boolean values are `0` or `1`. Values are validated at startup. An optional
`SPRINTATHON_SETTINGS_PROFILE` can point to a settings profile prepared by the
benchmark's UI-backed helper. The corresponding `SPRINTATHON_BENCHMARK_*`
overrides, when present, take priority over the generic settings.
