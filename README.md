# Sprintathon

It's a Marathon, but also a sprint! 

Sprintathon is a gameplay-focused fork of [Aleph One](https://github.com/Aleph-One-Marathon/alephone) that adds a parkour-like moveset, unrestricted mouselook and other subjective modernizations to the Marathon engine while retaining compatibility with Marathon scenarios.

Sacrilegious features include JUMPING, CROUCHING, SPRINTING, SWIMMING ABOVE THE WATER SURFACE and more!

![Sprintathon gameplay](mthon.webp)

## Download

The easiest way to play Sprintathon on Linux is with the prebuilt Flatpak bundle:

**[Download the latest Sprintathon release](https://github.com/visageeee/alephone-sprintathon/releases/latest)**

Install the downloaded bundle with:

```bash
flatpak install --user ./Sprintathon-*-x86_64.flatpak
```
Launch Sprintathon from your application menu or run:

```bash
flatpak run io.github.visageeee.Sprintathon
```

The packaged launcher lets you select which installed Marathon scenario to play.

## Features

Gameplay additions can be configured from the **Sprintathon** preferences panel. Most movement features can be enabled or disabled individually.

### Parkour Movement

- Jumping! With "coyote time"
- Crouching and crouch long-jumps
- Sprinting with a recharging stamina meter
- Sprint-slide attack
- Dodge dives
- Jump kicks and roundhouse kicks
- Wall kicks for parkour
- Wall-running and wall-jumping
- Modernized swimming - No more bobbing
- Water and dry ledge-grabbing
- Mantling

### Camera and presentation

- **Full unrestricted vertical mouselook with added smoothing**
- Weapon sway
- Legs
- Footstep sounds synced with movement
- Stronger movement and impact feedback
- Optional global fog
- Revamped Preferences and Level Select interfaces

### Combat

- Magazine-based reloading
- Sprint, slide and airborne attacks
- Bullet time with audiovisual feedback


Movement features can be combined freely, allowing Sprintathon to range from mostly traditional Marathon movement to the complete modernized moveset.


## Full unrestricted mouselook

Sprintathon’s extended mouselook uses true 3D camera rotation at every angle, including straight up and down. Aleph One’s original renderer relies on a forward-facing 2D portal system, which caused missing polygons, black corners, and smearing at steep viewing angles. Sprintathon expands visibility checks around the full horizon, bypasses incompatible legacy clipping planes, and lets OpenGL’s 3D frustum and depth buffer handle clipping and occlusion. Strict sprite depth testing also prevents enemies and effects from appearing through walls.


## Bullet time

Bullet time temporarily slows the action while leaving the player responsive.

While active it applies:

- Radial motion blur
- Increased contrast and color grading
- Slight FOV narrowing
- Slowed gameplay sounds
- Sound effects

If enabled Bullet time is triggered with the "B" key.


## Realistic Reloading

Sprintathon adds a weapon-reload binding, set to **R** by default. Reloading does not discard unfinished magazines. The game remembers the rounds left in every magazine, uses full magazines first, and later returns the fullest partial magazine available. Reload is ignored when the magazine already loaded is the fullest one available.

## Building

Clone the Sprintathon branch. The scenario submodules provide the original
Marathon game data used by packaged builds:

```bash
git clone --branch sprintathon --single-branch --recurse-submodules \
  https://github.com/visageeee/alephone-sprintathon.git sprintathon
cd sprintathon
```

The scenario submodules are optional when building only the engine and using
game data already installed elsewhere.

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

Package names vary on Arch, openSUSE, FreeBSD and other systems. Install a C++17
compiler, Autoconf, Automake and development packages for Boost, ASIO, SDL2,
SDL2_image, SDL2_ttf, zlib, libpng, libsndfile and OpenAL. Curl, miniupnpc,
zziplib, VPX, Matroska, EBML, Vorbis and libyuv enable optional features.

### Compile on Linux or FreeBSD

A Git clone does not contain the generated `configure` script. Generate it,
configure the project and compile:

```bash
autoreconf -i
./configure
make -j"$(nproc)"
```

The resulting executable is `Source_Files/alephone`. Installation is optional:

```bash
sudo make install
```

### Windows

Install Visual Studio 2022 with the **Desktop development with C++** workload,
Git and [vcpkg](https://github.com/microsoft/vcpkg). Bootstrap vcpkg and enable
Visual Studio integration:

```powershell
git clone https://github.com/microsoft/vcpkg C:\src\vcpkg
C:\src\vcpkg\bootstrap-vcpkg.bat
C:\src\vcpkg\vcpkg integrate install
```

Clone Sprintathon with submodules, open `VisualStudio/AlephOne.sln`, select an
x64 configuration and build the `AlephOne` project. Keeping both vcpkg and the
source tree in short paths without spaces avoids several dependency build
problems.

### macOS

Install Xcode command-line tools and vcpkg, then clone Sprintathon with
submodules. Aleph One provides `vcpkg/install-arm-osx.sh` and
`vcpkg/install-x64-osx.sh` for Apple Silicon and Intel respectively. After
installing the appropriate dependencies, open `Xcode/AlephOne.xcodeproj` in
Xcode and build the desired target.

The Windows and macOS paths have not yet received the same fresh-build testing
as the Linux build. Please report Sprintathon-specific failures on GitHub.

## Running

Aleph One requires Marathon scenario data, including files such as `Map`, `Shapes`, `Sounds`, and `Images`.

If you store them in `~/Games/Marathon`, pass that scenario directory to the locally compiled executable:

```bash
./Source_Files/alephone ~/Games/Marathon
```

Be sure to run `./Source_Files/alephone`, older system installations could reside at `/usr/local/bin/alephone`.

## Controls

Configure bindings from the in-game keyboard preferences. Sprintathon adds or repurposes bindings for:

- **Space** — Jump / Swim
- **C** — Crouch
- **Left Shift** — Sprint
- **R** — Reload
- **Q / E** — Previous / Next Weapon

The exact keys are user-configurable.

## Configuration

Open Preferences and select the **Sprintathon** section. It contains:

* A master movement toggle
* Individual movement-feature toggles
* Several extended-mouselook limits, including nearly full upward and downward aiming

Global fog is configured separately in the OpenGL graphics preferences.

## Project status

Sprintathon is experimental. Gameplay behavior, networking compatibility, saved preferences, and scenario-specific interactions may still require testing.


## Upstream project

Sprintathon is based on Aleph One, the open-source continuation of the Marathon engine.

For upstream documentation, licensing, credits, and additional platform-specific build information, see the [Aleph One project](https://github.com/Aleph-One-Marathon/alephone).

## License

Sprintathon retains Aleph One's existing licensing. See the repository's license and copyright files for details.
