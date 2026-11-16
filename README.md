# Mario Kart 64 PC Ultimate — Ricky Gameplays

Unofficial fork of [HarbourMasters/SpaghettiKart](https://github.com/HarbourMasters/SpaghettiKart). Original PC port and engine by HarbourMasters and the SpaghettiKart contributors; customization by Ricky Gameplays.

## Added features

- **Up to 16 local players** in Grand Prix, VS and Battle, with optional bots filling race grids.
- **AFK takeover:** inactive players become bots after a configurable timeout; controller input immediately restores human control. Bots can use items, with separate race and Battle behavior.
- **1, 2 or 3 monitors**, improved split-screen layouts, duplicate character selection and saved controller-device assignments.
- Optional **FPS counter** and **hold Tab to fast-forward**.
- Adjustable **minimap size and transparency**, including player markers, with a separate shared map on each selected monitor.
- Expanded results, standings, 9th–16th place graphics and Grand Prix ceremonies.
- Additional characters and courses, with experimental course-specific improvements.
- Multiplayer HUD, audio, fullscreen/monitor-switching and visual fixes, plus a live Luigi Raceway video screen.
- Faster item-box respawns and reduced lightning frequency in 16-player mode; configurable race bomb obstacles.
- Optional detailed crash logging and a Grand Prix finale shortcut.

## Defaults

8-player and 16-player options, Grand Prix bots, AFK takeover, Tab fast-forward and the Luigi Raceway multiplayer screen are enabled. AFK timeout is **5 seconds**; minimap size is **85%** with **25% transparency**. FPS counter and detailed logging default off. Existing saved settings override fresh defaults.

## Download and setup

Download the [latest release (v0.4.0)](../../releases/latest) for Windows, Linux or macOS, extract the archive and follow the included instructions. On Windows, run `Launch-Combined.cmd` or `Spaghettify.exe`. Open **Escape → Multiplayer → Local play** for multiplayer options.

**A supported US Mario Kart 64 ROM is required separately.** The release does not include the ROM, extracted game archive (`mk64.o2r`), saves, personal settings or logs. Keep the executable and the supplied port resource archive together.

Extended multiplayer, Battle AI and some added course features remain experimental. See the release notes for platform-specific details and testing status.

Recent local development includes widescreen menu artwork and character-set switches; these changes are not yet part of a published release.

## Source and building

Clone this repository with submodules and follow [the build guide](docs/BUILDING.md). The original project documentation and credits are preserved in [the upstream README](docs/UPSTREAM-README.md). The controller dependency patch is applied by CMake during configuration.
