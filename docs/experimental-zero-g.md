# Experimental zero gravity

Opt-in, player-only zero gravity for Sprintathon's OpenGL shader renderer.

Apply the patch to the current Sprintathon source, then rebuild:

```bash
cd ~/Projects/alephone &&
git apply --check ~/Downloads/sprintathon-zero-g/sprintathon-zero-g.patch &&
git apply ~/Downloads/sprintathon-zero-g/sprintathon-zero-g.patch &&
make -j"$(nproc)"
```

Launch a normal scenario with the experiment enabled:

```bash
SPRINTATHON_ZERO_G=1 ./Source_Files/sprintathon "$HOME/Games/mi"
```

Start a new game in a reasonably open level. A normal launch without the
variable restores normal movement; this patch does not change saved preferences.

## Surface-kick mode

Use `SPRINTATHON_ZERO_G=2` for zero gravity without thrusters or braking.
Forward/back/strafe and crouch cannot change velocity. Mouse rotation,
sprint + strafe roll, and weapon recoil remain available.

Press jump within 1/8 world unit of a floor, ceiling or blocking wall to push
away from it. Each kick adds an outward impulse of 0.18 world units/tick while
preserving sideways drift. A fresh press is required; holding jump cannot
produce repeated kicks or queue a kick for a later collision. At corners the
nearest detected surface wins. Jump + crouch does not brake in this mode.

The short wall probes use the normal player collision body. Open portals and
surfaces outside reach provide no foothold. The body remains upright, as in
mode 1. Standalone tests cover mode separation, no air thrust/braking, kick
latching, impulse directions, retained tangential drift and contact probes;
full engine and in-game verification remain outstanding for this addition.

`SPRINTATHON_ZERO_G=1` keeps the original thruster mode described below.

## Controls (thruster mode)

| Existing binding | Zero-g action |
| --- | --- |
| Mouse | Rotate around your local yaw/pitch axes, with no pitch stop or auto-level |
| Forward/backward | Thrust forwards/backwards relative to the camera |
| Strafe left/right | Sideways thrust relative to the camera |
| Jump | Thrust upwards relative to the camera |
| Crouch | Thrust downwards relative to the camera |
| Sprint + strafe | Roll left/right instead of strafing |
| Jump + crouch together | Brake; release movement controls to stop completely |
| Fire | Recoil pushes backwards along your aim direction |

Releasing movement controls preserves velocity. Looking around changes the
thrust direction, not your existing drift. Roll stops when you release the roll
controls, but your orientation stays where you left it. Mouse rotation remains
directly controlled rather than adding persistent angular momentum.

Successful non-melee shots produce one recoil impulse per firing event; pellet
counts do not multiply the impulse. Weapon recoil definitions determine strength,
with a small minimum for weapons that ordinarily have little or no recoil.
Empty shots and melee attacks produce no zero-g recoil.

Thrust acceleration is 0.007 world units/tick²; speed is capped at 0.35 world
units/tick (about 10.5 world units/second at 30 ticks/second). Bullet time scales
movement, thrust and roll input. Mouse turning keeps its normal response.

Walls remove the velocity pushing into them while retaining tangential motion.
Floor and ceiling impacts stop vertical motion. The established player collision
body stays upright, regardless of camera roll. This is not yet a rotating body
that can squeeze through passages sideways. Ordinary liquids and damage rules
still apply, and monsters retain ordinary movement.

## Implementation and testing

Orientation uses normalized quaternions with spherical interpolation, avoiding
an Euler-angle flip when passing straight up/down. Physics, thrust and recoil
use the same orientation; the renderer receives equivalent yaw, pitch and roll.
The tick-interpolated quaternion camera replaces the usual render-time mouse
preview in this mode, so mouse feel may differ slightly from normal play.

The legacy portal renderer assumes an upright view. Zero-g uses its existing
conservative full-circle visibility path; this can cost performance. The optional
batch renderer remains available. GPU depth testing still handles occlusion.

Standalone C++ checks passed for orientation round trips through poles, long
rotation sequences, camera-relative thrust, persistent drift, speed limiting,
braking, recoil direction, interpolation and collision response. Runtime tests
used mock engine types; collision tests compiled the actual added collision
block. A full engine build and in-game testing have not been performed here.

Network play, replay playback and dead players use normal physics. This mode is
experimental and is not represented in replay data. Orientation state resets on
level load and respawn; roll is not serialized into save files. Use new games for
testing. Scripted, editor and screenshot cameras retain their own view controls.

No Myth assets or importer are required. The incremental patch was prepared
against the source containing the earlier terrain collision/crater changes and
landscape-roll fix.
