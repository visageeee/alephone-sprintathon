# Generated normal maps

In Graphics → Textures, enable **Generated normal maps** and adjust **Generated
normal strength** (default 25%). The feature is off by default. The 25% setting
uses the generated slope as-is; 100% amplifies it fourfold. The slider also
increases the contrast of the generated normal response to the flashlight,
which can exaggerate painted texture details at high settings. Toggling it
while a level is open reloads rendering resources; changing strength does not
regenerate textures. These maps affect flashlight incidence and add a small, broad flashlight
highlight. They do not alter geometry, shadow visibility, parallax, or authored
material properties.

During level loading the preparer skips authored bump maps, non-opaque RGBA
images, landscapes, blended texture options, and descriptors used by
transparent sides or media. It uses decoded source pixels before runtime
upscaling where available. Animated or late-loaded textures that were not
prepared at level load retain their existing appearance rather than generating
maps during gameplay. Compressed/non-RGBA sources and textures larger than
2,048 pixels in either dimension are skipped.

Wrapped low-pass and broad-background filters suppress small pixel noise and
large brightness gradients before deriving tangent-space normals. The generator
retains enough local slope to make the effect visible under a moving light. Mip
levels average decoded vectors and renormalize them. RGB stores the normal;
alpha remains the engine's neutral bump height of 128.

The per-user image-cache directory contains `Sprintathon Generated Normals v1`,
limited to 128 MiB. Cache keys incorporate source pixels, dimensions, wrapping
and generator version; changing the algorithm requires increasing that version.
Invalid entries are regenerated; an unwritable cache falls back to generation in
memory. Generated GPU textures have a separate 128 MiB budget. Texture arrays
can use additional GPU memory under their existing 128 MiB array budget, and
loading temporarily retains CPU mip data for array uploads. Generated maps in
arrays are uploaded from that CPU data; compatible colour textures use a GPU
copy. If the GPU copy is unavailable or incompatible, those materials fall back
to ordinary 2D textures.

The optional environment variable `SPRINTATHON_PREPARE_GENERATED_NORMALS=1`
prepares maps during loading even if the Settings checkbox is off, without
turning on their visual effect. This can be useful for warming the cache.

When enabled, level loading reports generated-normal preparation counts to the
application log and terminal. With batch rendering enabled, the terminal also
reports whether the shader's strength uniform is available and how many
materials use generated normal arrays. A zero-upload count indicates a fallback,
not proof that normals are being applied. One-time draw diagnostics in the
application log report the number of batched surfaces using generated maps and
whether the flashlight was on. Bright ambient lighting can saturate the final
lighting and conceal changes; compare surfaces in a darker area as well.

For manual diagnosis of the batched renderer, start the game with
`SPRINTATHON_NORMAL_DEBUG=1` to display the generated normal texture as false
colour, independently of the flashlight. Use `SPRINTATHON_NORMAL_DEBUG=2` to
show the flashlight's mapped-versus-flat difference: red means darker and green
means brighter (amplified for inspection). The flashlight must be on for mode
2. These modes replace surface colours and are not normal gameplay settings.
Unset the variable to restore normal rendering.
