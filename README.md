# Sprintathon

**It's a Marathon, but also a sprint!**

Sprintathon is a gameplay-focused fork of [Aleph One](https://github.com/Aleph-One-Marathon/alephone) that brings parkour movement, unrestricted mouselook, modernized combat and extensive graphical options to the Marathon engine.

It remains compatible with Marathon scenarios - but playing them as intended is another matter.

Blasphemous features include **JUMPING, CROUCHING, SPRINTING, SLIDING, WALL-RUNNING, DODGE DIVES, CARTWHEELS, BACKFLIPS, RELOADING, BULLET TIME, PISTOL SCOPING,** and even **SWIMMING ABOVE THE WATER SURFACE**.

![Sprintathon gameplay](mthon.webp)

## Download and Install instructions

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

Download the macOS ZIP matching your Mac's processor:

- **ARM / Apple Silicon** for M1, M2, M3 and later Apple chips
- **x64 / Intel** for Intel-based Macs

Extract the ZIP and move `Sprintathon.app` to `Applications` or another writable
folder. The application is currently **unsigned and not notarized**, so macOS
Gatekeeper may block its first launch. First try control-clicking the app,
choosing **Open**, and confirming **Open** again.


Windows and macOS packages are less extensively tested than the Linux Flatpak.
Bug reports should include the operating-system version, processor architecture
and the exact release filename.

## Features

Gameplay additions can be configured from the **Sprintathon** preferences section. Most features can be enabled or disabled individually.

### Parkour movement

- Jumping with jump buffering and coyote time
- Crouching and crouch long-jumps
- Sprinting with recharging stamina
- Sprint slides and slide attacks
- Sideways dodge dives and backward dodge dives
- Cartwheels from sideways dodges
- Backflips from backward dodges
- Jump kicks, flying kicks and roundhouse kicks
- Wall kicks, wall-running and wall-jumping
- Modernized surface swimming
- Water and dry ledge-grabbing
- Mantling

### Camera and presentation

- Full unrestricted vertical mouselook with smoothing
- Weapon sway, movement lag and recoil feedback
- First-person legs during slides and dodges
- Footstep and movement sounds
- Optional Sprintathon HUD with new weapon switcher.
- Stronger movement, landing and impact feedback
- Revamped Preferences and Level Select interfaces
- Access to Preferences while a game is running

### Combat

- Magazine-based reloading with persistent partial magazines
- Sprint, slide, dodge and airborne attacks
- Bullet time with audiovisual feedback
- Optional automatic bullet time during dodge moves
- Optional scope for a single .44 Magnum pistol

### Graphics

- Optional global fog
- Media-relative height fog
- Animated fog density and weather presets
- Fog depth, darkness and distance controls
- Transparent-liquid opacity control
- Animated media ripples with individual speeds for each media type
- Adjustable ripple and wet-texture strength
- Under-liquid screen distortion
- Refraction, distortion and magnification through transparent liquid surfaces
- Liquid animation that follows bullet time

Movement and presentation features can be combined freely, allowing Sprintathon to range from mostly traditional Marathon movement to the complete modernized moveset.

## Full unrestricted mouselook

Sprintathon's extended mouselook uses true 3D camera rotation at every angle, including straight up and down. Aleph One's original renderer relies on a forward-facing 2D portal system, which caused missing polygons, black corners and smearing at steep viewing angles.

Sprintathon expands visibility checks around the full horizon, bypasses incompatible legacy clipping planes and lets OpenGL's 3D frustum and depth buffer handle clipping and occlusion. Strict sprite depth testing also prevents enemies and effects from appearing through walls.

## Bullet time

Bullet time temporarily slows the action while leaving the player responsive. While active it applies:

- Radial motion blur
- Increased contrast and color grading
- Slight FOV narrowing
- Slowed gameplay sounds and liquid animation
- Slowdown, speedup and heartbeat audio

Bullet time is bound to **B** by default. It can also be configured to activate automatically during dodge moves.

## Realistic reloading

Sprintathon adds a weapon-reload binding, set to **R** by default. Reloading does not discard unfinished magazines. The game remembers the rounds left in every magazine, uses full magazines first and later returns the fullest partial magazine available. Reloading is ignored when the loaded magazine is already the fullest available.

## Pistol scope

When enabled, the secondary trigger activates a scope while carrying only one .44 Magnum pistol. The scope provides increased magnification, hides the first-person weapon and adds aiming sway and visible recoil. Reloading temporarily leaves the scoped view. The normal secondary weapon remains available while carrying two pistols.

## Sprintathon HUD

The optional Sprintathon HUD is installed with packaged and `make install` builds. It provides a circular motion tracker, compact health and oxygen meters, weapon silhouettes, magazine counts and weapon-specific ammunition graphics.

Select it from the HUD or plugin preferences after installation.

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

- **Space** â€” Jump / Swim
- **C** â€” Crouch; press during a dodge for a cartwheel or backflip
- **Left Shift** â€” Sprint
- **R** â€” Reload
- **B** â€” Bullet time
- **Q / E** â€” Previous / Next Weapon
- **Secondary trigger with one pistol** â€” Scope, when enabled

The exact keys are user-configurable.

## Configuration

Open Preferences and select **Sprintathon** for movement, stamina, dodge, bullet-time and pistol-scope options.

The main **Graphics** section contains separate **Rendering**, **Textures**, **Liquids** and **Fog** tabs. These include the extended fog, transparency, ripple, wet-texture and refraction controls.

Preferences can also be opened during a game. Saving or cancelling returns directly to the running game.

## Project status

Sprintathon is experimental. Gameplay behavior, networking compatibility, saved preferences and scenario-specific interactions still require testing. Windows and macOS builds are produced through automated workflows, but feedback from testing on those platforms is especially welcome.

## Upstream project

Sprintathon is based on Aleph One, the open-source continuation of the Marathon engine.

For upstream documentation, licensing, credits and additional platform-specific build information, see the [Aleph One project](https://github.com/Aleph-One-Marathon/alephone).

## License

Sprintathon retains Aleph One's existing licensing. See the repository's license and copyright files for details.

