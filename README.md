# Sprintathon

**It's a Marathon, but also a sprint!**

Sprintathon is an experimental fork of [Aleph One](https://github.com/Aleph-One-Marathon/alephone) that brings parkour movement, unrestricted mouselook, expanded combat features, and extensive graphical upgrades to the Marathon engine.

Slide through a firefight, light a dark corridor with a flare, or watch colored projectile light sweep across the walls. Sprintathon combines a faster, more physical moveset with new lighting, atmospheric effects, sound, and quality-of-life features—all configurable in the preferences.

It remains compatible with Marathon scenarios - but playing them as intended is another matter.

Blasphemous features include **JUMPING, CROUCHING, SPRINTING, SLIDING, WALL-RUNNING, DODGE DIVES, CARTWHEELS, BACKFLIPS, RELOADING, BULLET TIME, PISTOL SCOPING,** and even **SWIMMING ABOVE THE WATER SURFACE**.

![Sprintathon gameplay](mthon.webp)

## Download and installation

Prebuilt releases are available from GitHub:

**[Download the latest Sprintathon release](https://github.com/visageeee/alephone-sprintathon/releases/latest)**

Downloads are provided separately for Linux, Windows and macOS. These are
independent packages, so choose the one for your operating system.

### Linux Flatpak

Install the downloaded bundle with:

```bash
flatpak install --user --reinstall ./Sprintathon-*-x86_64.flatpak
```

Launch Sprintathon from the application menu or run:

```bash
flatpak run --branch=stable io.github.visageeee.Sprintathon
```


### Windows

Download the Windows ZIP for your system - normally the 64-bit (`x64`) package -
and extract the complete archive to a writable folder. Keep the executable and
its accompanying data directories together, then run `Sprintathon.exe`.

Sprintathon's Windows builds are currently **unsigned**. Windows SmartScreen may
therefore display an "unknown publisher" warning even when the archive was
downloaded from the official Sprintathon release page. Review the filename and
release source before choosing **More info - Run anyway**. You can alternatively
right-click the downloaded ZIP, open **Properties**, select **Unblock** if the
option is present, and then extract it.

Do not download or bypass warnings for Sprintathon packages obtained from
untrusted mirrors.

### macOS

Download the macOS ZIP.

Extract the ZIP and move `Sprintathon.app` to `Applications` or another writable
folder. The application is currently **unsigned and not notarized**, so macOS
Gatekeeper may block its first launch. First try control-clicking the app,
choosing **Open**, and confirming **Open** again.


Windows and macOS packages are less extensively tested than the Linux Flatpak.
Bug reports should include the operating-system version, processor architecture
and the exact release filename.

## Features

Most additions can be enabled or disabled individually. Start with a graphics preset—**Low**, **Medium**, **High**, or **Total Sprintathon**—then adjust individual effects to suit your taste and hardware. Changing a graphics option selects **Custom**.

### Graphics and atmosphere

Sprintathon expands the engine's lighting and surface effects while retaining the original scenario artwork.

**Dynamic colored lighting**

Experimental feature adding “per-pixel” colored lighting emitted from projectiles, bright textures, and scenery like light fixtures.

- Projectiles and explosions illuminate nearby surfaces with colored light.
- Bright areas of wall textures and scenery sprites can act as light sources, as does lava.
- Separate controls with adjustable intensity, reach, render distance, and light counts.
- Dynamic lighting can be taxing for GPU's and is enabled by the **High** and **Total Sprintathon** presets.

**Shadows and light effects**

- Character sprite shadows and adjustable ambient occlusion.
- Optional blending of sector shading to soften abrupt lighting boundaries.
- Shafts of light from the landscape backgrounds and anamorphic lens flares from bright textures, sprites and projectiles..
- Updated invisibility with a refractive look.

**Fog and liquids**

- Enhanced fog rendering, media-relative fog, animated density, drifting fog, and weather presets.
- Enhanced liquid effects. Watch the surface ripple and the depths distort and refract.

**Texture detail and motion**

- Optional 2xSaI upscaling for sprites and wall textures, alongside filtering controls.
- Blurred projectile motion trails.

Effects can be combined freely. Dynamic lighting and large numbers of scenery lights can be demanding, especially in complex maps with long sightlines; their individual controls let you tune the appearance and performance.

### Parkour movement

- Jumping with jump buffering and coyote time.
- Crouching, crouch long-jumps, and sprinting with recharging stamina.
- Sprint slides and slide attacks.
- Forward rolls from a double-tap during a slide, with chained rolls.
- Sideways and backward dodge dives, cartwheels, and backflips.
- Jump kicks, flying kicks, roundhouse kicks, and wall kicks.
- Wall-running and wall-jumping.
- Surface swimming, ledge-grabbing, and mantling on land and out of water.

### Combat and flares

- Magazine-based reloading that retains partially used magazines.
- Sprint, slide, dodge, and airborne attacks.
- Bullet time, with optional automatic activation during dodges.
- Use the scope when carrying a single .44 Magnum pistol.
- Optional corpse physics and blood effects when corpses are hit.
- Droppable flares with flickering red light for those dark places.

### Camera, sound, and presentation

- Unrestricted vertical mouselook, smoothing, and an invert vertical axis option.
- Weapon sway, movement lag, recoil, and landing feedback.
- First-person legs during slides, dodges, and rolls.
- Footstep and movement sounds.
- Optional cavern echo and reverberation for large enclosed spaces.
- A new Sprintathon HUD with a self-hiding weapon list.
- Screenshot mode that pauses the game and frees the camera for exploring a scene or composing a shot.
- Revised Preferences and Level Select interfaces, with preferences accessible during play.

### Checkpoints and reloads

Optional Halo-style **Checkpoints** create automatic saves during quiet moments between fights.

Reloading within the same level can retain unchanged sprite textures on the GPU, avoiding repeated sprite upscaling and uploads.

## Full unrestricted mouselook

Sprintathon's extended mouselook uses true 3D camera rotation at every angle, including straight up and down. Aleph One's original renderer relies on a forward-facing 2D portal system, which caused missing polygons, black corners and smearing at steep viewing angles.

Sprintathon expands visibility checks around the full horizon, bypasses incompatible legacy clipping planes and lets OpenGL's 3D frustum and depth buffer handle clipping and occlusion. Strict sprite depth testing also prevents enemies and effects from appearing through walls.


## Building

Clone the Sprintathon branch. Scenario submodules provide the original Marathon game data used by packaged builds:

```bash
git clone --branch sprintathon --single-branch --recurse-submodules \
  https://github.com/visageeee/alephone-sprintathon.git sprintathon
cd sprintathon
```

The scenario submodules are optional when building only the engine and using game data installed elsewhere.

### Ubuntu and Debian-based distributions

```bash
sudo apt update
sudo apt install \
  build-essential autoconf autoconf-archive automake libtool pkg-config \
  libboost-all-dev libasio-dev \
  libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev \
  libzzip-dev libpng-dev libcurl4-gnutls-dev libminiupnpc-dev \
  libopenal-dev libsndfile1-dev libglu1-mesa-dev \
  libvpx-dev libmatroska-dev libebml-dev \
  libvorbis-dev libvorbisenc2 libyuv-dev
```

### Fedora

Enable RPM Fusion first if optional multimedia packages are unavailable, then:

```bash
sudo dnf install \
  autoconf autoconf-archive automake libtool make gcc-c++ pkgconf-pkg-config \
  boost-devel asio-devel SDL2-devel SDL2_image-devel SDL2_ttf-devel \
  libpng-devel libcurl-devel zziplib-devel miniupnpc-devel \
  openal-soft-devel libsndfile-devel mesa-libGLU-devel
```

Package names vary on Arch, openSUSE, FreeBSD and other systems. Install a C++17 compiler, Autoconf, Automake and development packages for Boost, ASIO, SDL2, SDL2_image, SDL2_ttf, zlib, libpng, libsndfile and OpenAL. Curl, miniupnpc, zziplib, VPX, Matroska, EBML, Vorbis and libyuv enable optional features.

### Compile on Linux or FreeBSD

A Git clone does not contain the generated `configure` script. Generate it, configure the project and compile:

```bash
autoreconf -fi
./configure
make -j"$(nproc)"
```

The resulting executable is `Source_Files/sprintathon`. Installation is optional:

```bash
sudo make install
```

### Windows

Install Visual Studio 2022 with the **Desktop development with C++** workload, Git and [vcpkg](https://github.com/microsoft/vcpkg). Bootstrap vcpkg and enable Visual Studio integration:

```powershell
git clone https://github.com/microsoft/vcpkg C:\src\vcpkg
C:\src\vcpkg\bootstrap-vcpkg.bat
C:\src\vcpkg\vcpkg integrate install
```

Clone Sprintathon with submodules, open `VisualStudio/AlephOne.sln`, select `Release` and `x64`, then build the `AlephOne` project. The resulting executable is `VisualStudio/x64/Release/Sprintathon.exe`.

To create a distributable package containing the executable, documentation, HUD and all Sprintathon assets, open PowerShell in the `VisualStudio` directory after building and run:

```powershell
.\dist-windows.ps1 -x64 $true -a1 $true -output_path .\dist
```

Do not distribute the executable by itself. The generated package includes the data required by Sprintathon's visual and audio effects.

### macOS

Install Xcode command-line tools and vcpkg, then clone Sprintathon with submodules. Run `vcpkg/install-arm-osx.sh` on Apple Silicon or `vcpkg/install-x64-osx.sh` on Intel to install the appropriate dependencies.

Open `Xcode/AlephOne.xcodeproj`, select the **Aleph One** scheme and **Release** configuration, then build. The scheme retains its upstream name, but its product is `Sprintathon.app`. Its application bundle includes the Sprintathon runtime assets and HUD.

A reproducible command-line build can use a local Derived Data folder:

```bash
cd Xcode
xcodebuild \
  -project AlephOne.xcodeproj \
  -scheme "Aleph One" \
  -configuration Release \
  -derivedDataPath build \
  CODE_SIGNING_ALLOWED=NO \
  build
```

The resulting application is `Xcode/build/Build/Products/Release/Sprintathon.app`. Create a distributable ZIP while preserving macOS metadata with:

```bash
ditto -c -k --sequesterRsrc --keepParent \
  build/Build/Products/Release/Sprintathon.app \
  Sprintathon-macOS.zip
```

This produces an unsigned build suitable for local testing. Public macOS distribution additionally requires signing with an Apple Developer identity and notarizing the archive.

## Running a local build

Aleph One requires Marathon scenario data, including files such as `Map`, `Shapes`, `Sounds` and `Images`.

If they are stored in `~/Games/Marathon`, pass that scenario directory to the locally compiled executable:

```bash
./Source_Files/sprintathon ~/Games/Marathon
```

Be sure to run `./Source_Files/sprintathon`; an older Aleph One or Sprintathon build may still be installed as `/usr/local/bin/alephone`.

## Controls

Configure bindings from the in-game keyboard preferences. Default Sprintathon controls include:

- **Space** - Jump / Swim
- **C** - Crouch; press during a dodge for a cartwheel or backflip
- **Left Shift** - Sprint
- **R** - Reload
- **B** - Bullet time
- **Q / E** - Previous / Next Weapon
- **Secondary trigger with one pistol** - Scope, when enabled

The exact keys are user-configurable.

## Configuration

Open **Preferences → Sprintathon** for movement, combat, stamina, and effects settings. **Sound** contains the Cavern Echo option, and **Controls** contains the configurable bindings, including screenshot mode.

**Graphics** contains **Presets**, **Display**, **Rendering**, **Light FX**, **Dynamic Lighting**, **Textures**, **Liquids**, and **Fog** tabs. High and Total Sprintathon enable dynamic lighting; individual controls let you customize the result.

**Display** includes Player Light Circle, which defaults off, and Skip Intros and Fades, which defaults on. New defaults do not overwrite existing saved preferences.

In screenshot mode, **Q / E** rotate the camera, while your bound **Jump** and **Crouch** controls move it up and down.

Preferences can also be opened during a game. Saving or cancelling returns directly to the running game.

## Project status

Sprintathon is experimental. Gameplay behavior, networking compatibility, saved preferences and scenario-specific interactions still require testing. Windows and macOS builds are produced through automated workflows, but feedback from testing on those platforms is especially welcome.

## Upstream project

Sprintathon is based on Aleph One, the open-source continuation of the Marathon engine.

For upstream documentation, licensing, credits and additional platform-specific build information, see the [Aleph One project](https://github.com/Aleph-One-Marathon/alephone).

## License

Sprintathon retains Aleph One's existing licensing. See the repository's license and copyright files for details.

