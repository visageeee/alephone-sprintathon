# Visual texturing, lighting and scenery mode

Start a single-player level and press **Shift+F8**. The game pauses, weapons and
the normal HUD disappear, and two vertical palettes take over: textures on the
left and scenery on the right, with a narrow light-swatch column to the left of
the textures. The editor starts with a free mouse pointer.
Choose a texture, then click directly on a surface anywhere in the view to paint
it. Choose a scenery sprite, then click a floor to place it. Hanging scenery
types are marked "ceiling" and are placed by clicking a ceiling.

The **Texture** and **Lighting** checkboxes at bottom left control which surface
properties a click applies. Enable either one or both. Neither enabled means
surface painting does nothing. Initially Texture is enabled and Lighting is
disabled. Checkbox choices are retained when reopening the editor during the
same application run. Scenery placement and the eraser do not use these switches.

The narrow light column has ten steady brightness swatches, ordered from full
brightness at the top to darkness at the bottom. A matching constant light is
reused or created when painting; existing shared light definitions are never
modified. Right-click sampling selects the nearest brightness step.

Click the **Light/Texture** or **Items** header to collapse that panel to its
header bar; click again to expand it. Collapsing the left panel also hides its
checkboxes. Selections and checkbox states are retained while collapsed, and
the uncovered area remains available for painting.

The first scenery entry is **Eraser**. Select it and click scenery to remove it.
Command-help text has been removed from the editor; controls are listed here.

| Control | Action |
| --- | --- |
| W/A/S/D (camera mode) | Move the camera |
| Bound Jump / Crouch controls (camera mode) | Move up / down |
| Shift | Faster camera movement |
| Mouse | Point at surfaces; look around in camera mode |
| Left click | Choose a palette entry, paint a surface, or place/erase scenery |
| Ctrl + left-drag (free cursor) | Slide the grabbed surface texture; release to finish |
| Right click | Sample the targeted surface texture and light |
| Wheel (camera mode) | Select previous / next entry in the active palette |
| Tab | Toggle free cursor / camera movement and aiming |
| Wheel over a palette | Previous / next page in that palette |
| Alt while aiming | Pick through transparent walls |
| Ctrl+Z / Ctrl+Y | Undo / redo (Ctrl+Shift+Z also redoes) |
| Ctrl+S | Save to the current file |
| Ctrl+Shift+S | Save As: export the edited level to a new file |
| Escape / Shift+F8 | Return to playing |

On macOS, Command also works for save and undo/redo.

The palette includes every frame in the texture collections referenced by the
current level's floors, ceilings and sides, including custom collections and
landscapes. Both palettes are paginated in groups of 16. OpenGL thumbnails use the active
texture replacements; software thumbnails use Shapes images.

The scenery palette uses the active scenario's scenery definitions, including
MML overrides, and shows types whose sprite collections are already loaded.
It previews the first animation frame, and placed objects use their normal
animation and collision behaviour. It does not load unrelated sprite collections.
Placement is part of undo/redo and is stored in the map's initial object list,
so new scenery is included when exporting the level. The eraser also handles
pre-existing map scenery and updates the exported object list. Erasing and
placing scenery share the undo/redo history with surface painting. Runtime-only
scenery can be erased and restored in the current game, but was never part of
the saved map. Scenery picking uses the sprite's bounding rectangle, stops at
walls, and respects connected polygon spaces.

The cursor names the targeted floor, ceiling, wall, lower split wall or
transparent wall. Picking follows connected polygons and ignores creatures and
scenery for surface painting. Existing texture offsets and animation/scrolling
modes are retained; light assignments change only when Lighting is enabled.
Painting a sky texture switches the transfer mode to
landscape; painting an ordinary texture onto sky switches it back to normal.

Export creates a **single-level .sceA map**, using the engine's existing level
exporter. It does not package the scenario's Shapes, Sounds, plugins or other
levels. Save it with a new filename and open it with the same scenario assets.
The original scenario is unchanged unless you deliberately choose to overwrite
it in the save dialog.

Edits remain in the current running level when you exit the editor, but must
be exported to survive leaving/reloading it. Undo/redo covers the current
editing session and resets when you reopen the editor. This is a mouse/keyboard
single-player tool, disabled during network games and replay playback.

This version paints existing map surfaces and adds/removes scenery. It does not
create geometry or missing sides, edit liquid surfaces, change texture alignment,
or create/edit shared light definitions.

Scenery deletion validates the live object, its polygon list and attached object
before changing saved records. Invalid links report an editor message instead
of entering the engine's unchecked removal path. The interpolation snapshot is
refreshed after removal. The reported in-game crash still needs runtime
verification; these checks cover unsafe deletion inputs, not a reproduced crash.

Hold **Ctrl** and drag with the left mouse button to pan a wall, floor or ceiling
texture under the cursor. This moves the existing texture even if the scenery
brush is selected; it does not paint the selected swatch or change lighting.
Landscapes are excluded. A complete drag is one undo/redo action and texture
positions are included in exported maps. Switch to the free cursor with Tab
before dragging.

The **Align adjacent** checkbox at bottom left extends a drag to connected
surfaces with the same texture and transfer mode. Coplanar floors or ceilings
share an origin across neighboring polygons; wall sections align around the
room boundary and into directly adjoining polygons, accounting for edge lengths and differing top heights.
Different textures, opposite-facing walls, and disconnected geometry are unaffected.
Alignment follows a connected traversal; a closed wall loop can retain one seam
when its perimeter is not a whole number of texture repeats. Uncheck it to
position just the grabbed surface.

A separate action panel below **Items** provides **Save**, **Save As**, and
**Fog: On/Off**. It remains available when Items is collapsed. Save updates the
current level in the existing scenario, preserving other levels, directory
metadata, scripts and unknown tags. It writes a temporary file and replaces the
destination only after successful completion. Older map formats, overlay files
and saved games cannot be overwritten through Save; use Save As instead.

Save As exports one level to a chosen `.sceA` file and remembers that destination
for later Save clicks (and Ctrl+S) while editing this source level. Choosing the
original source path updates that level in place instead of discarding the
scenario's other levels. Fog toggles rendering in the editor only; the original
fog preference is restored when leaving the editor and is not saved in the map.
