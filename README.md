# DESCENT 3MASTERED

**An unofficial, preservation-first modernization of Descent 3.**

Current development release: **0.9**

DESCENT 3MASTERED keeps the original game, missions, mechanics, and overall
presentation intact while rebuilding the technology around them for modern
systems. The aim is closer to a careful modern remaster than a remake: improve
performance, fidelity, compatibility, and usability without redesigning the
game into something else.

This project does **not** include the commercial Descent 3 game data. A legally
obtained installation of Descent 3 is required.

## Project direction

- Preserve the original game's behavior and artistic identity.
- Prefer coherent modern rendering paths over permanent legacy special cases.
- Improve fidelity where the original intent is clear; exact historical
  quantization is not itself a goal.
- Treat visual correctness, gameplay correctness, and frame consistency as
  regressions to test—not acceptable costs of higher average performance.
- Continue consolidating rooms, polymodels, terrain, particles, and
  post-processing into maintainable renderer families where their requirements
  genuinely overlap.

The current performance target is at least 120 FPS at 1920x1080 with 8x MSAA,
2x SSAA, and the advanced rendering features enabled across the project's
representative save-game suite. This is a development target on the reference
hardware, not a universal hardware guarantee.

## Highlights

### Modern rendering

- OpenGL 4.5 core renderer with retained rendering for mine geometry and
  polymodels.
- GPU compute terrain path retaining Descent 3's distinct outdoor rendering
  behavior.
- Per-pixel lighting, specular lighting, ambient occlusion, bloom, soft
  particles, and motion blur.
- 2x/4x/8x MSAA, 2x/4x SSAA, and up to 16x anisotropic filtering.
- Modernized fog across room geometry, models, effects, decals, and other
  translucent content.
- Improved widescreen and ultrawide presentation, high-resolution skyboxes,
  and higher-detail selected models.
- Rendering diagnostics, deterministic background captures, and GPU/CPU
  performance markers used for regression testing.

### Platform and input

- Supported release target: 64-bit Windows.
- Raw mouse input and high-resolution input/presentation timing improvements.
- Native analog-key support for compatible Wooting keyboards while retaining
  ordinary keyboard bindings and behavior.
- OpenAL Soft audio with environmental reverb and optional headphone HRTF.
- Out-of-process 32-bit Osiris bridge for compatibility with legacy mission
  modules from the original 32-bit game ecosystem.

### Game and compatibility work

- Adjustable field of view, frame limit, resolution, aspect ratio, and modern
  fullscreen/window behavior.
- Custom difficulty dimensions for enemy AI, enemy/projectile speed, enemy
  toughness, and resources. Setting every dimension equally remains equivalent
  to the corresponding classic difficulty.
- Legacy multiplayer interoperability work alongside an extended protocol path
  for features that older clients cannot represent.
- Continued fixes for long-standing Descent 3 rendering, input, mission,
  save-game, and multiplayer issues.

## Requirements

- 64-bit Windows. Windows 10 and Windows 11 are the current test targets.
- A GPU and driver exposing OpenGL 4.5.
- A legally obtained Descent 3 installation, patched to 1.4 or otherwise
  containing the required game data.
- The Mercenary expansion data is optional and required only to play Mercenary.

The normal GL4 renderer is the supported path. The legacy OpenGL compatibility
renderer remains available through command-line troubleshooting options, but is
not the focus of current development.

## Installation

The safest approach is to create a separate directory for DESCENT 3MASTERED and
copy the required data from an existing Descent 3 installation. Do not test a
development build directly over the only copy of your original installation.

Required retail data normally includes:

- `d3.hog`
- `extra.hog`
- `extra13.hog`
- `missions/d3.mn3`
- `missions/d3_2.mn3`
- `missions/d3voice1.hog`
- `missions/d3voice2.hog`
- `missions/training.mn3`

For Mercenary, also copy:

- `merc.hog`
- `missions/merc.mn3`

For the original multiplayer content, also copy the `netgames` directory and
the relevant stock multiplayer missions, including `missions/Fury.mn3` and
`missions/bedlam.mn3`. The `movies` directory is optional.

Release archives should be extracted into that directory after the retail data
has been copied. Development builds must include `Descent3Mastered.exe`,
`descent3mastered.hog`, `descent3mastered-win.hog`, the runtime DLLs, game
modules, and legacy Osiris bridge components—not only the main executable.

By default, pilots, saves, screenshots, and other user-generated files are kept
under `Saved Games\DESCENT 3MASTERED`. On first launch, existing files from
`Saved Games\Piccu Engine` are copied into the new directory without deleting
the source or overwriting newer files. Portable installations use the marker
`descent3mastered_portable`; the old `piccu_portable` marker remains recognized
for migration compatibility.

## Building on Windows

The supported build is 64-bit Windows using CMake and the Microsoft Visual C++
toolchain. Install:

- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.11 or newer and Ninja (both are available through Visual Studio's
  CMake tooling)
- A Windows SDK supplied by Visual Studio

From a Developer PowerShell or command prompt:

```powershell
cmake -S . -B out/build/x64-Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/x64-Release --target Descent3Mastered
```

The build also produces or copies supporting files required at runtime. Keep
those files together when constructing a test installation.

The repository contains legacy Linux/macOS code inherited through its lineage,
but those platforms are not currently supported release targets and should not
be assumed to build or run correctly.

## Reporting issues

Please report reproducible problems through the
[DESCENT 3MASTERED issue tracker](https://github.com/hurleybird/DESCENT-3MASTERED/issues).
Include the affected mission/save, relevant video settings, GPU and driver, and
whether the issue reproduces with advanced effects disabled. Screenshots taken
with the engine's built-in capture facility are preferred.

## Lineage and independence

DESCENT 3MASTERED descends from the official Descent 3 source release and from
Piccu Engine, which also incorporated work from InjectD3 and other community
efforts. It is now an independent project with substantially different renderer,
platform, compatibility, and design goals. Piccu Engine remains an important
ancestor and a useful historical reference, but is no longer this repository's
upstream project.

## License and acknowledgements

The engine source is distributed under the GNU General Public License version 3;
see [LICENSE](LICENSE). Bundled third-party components retain their respective
licenses in their source or distribution directories.

Acknowledgements include:

- Project lead and DESCENT 3MASTERED development: Jeff Graw (hurleybird).
- Outrage Entertainment and the original Descent 3 developers.
- The official Descent 3 source-release contributors.
- Piccu Engine, InjectD3, and their contributors.
- D2X for MVE-related source work.
- GZDoom for the revision-checking CMake script.
- OpenAL Soft for the audio runtime.
- Wooting for the Analog SDK.

DESCENT 3MASTERED is an unofficial community project and is not affiliated with
or endorsed by the original game's publishers or rights holders.
