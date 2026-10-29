# PenumbraBlackPlague.FPSFix

> [!WARNING]
> This patch has been pretty much entirely been generated using AI; 
> I won't and will never claim to have enough knowledge or expertise to do this 
> kind of reverse-engineering by myself; I've tested about 3 levels in Black Plague and 2 in Requiem at 360hz + 
> briefly tested at 120, 144 and 240 and so far spotted 
> no major issues (on my machine ofc, if you have issues feel free to open an Issue).
> If you're interested and want more (human generated) info, I have a blog post [here](https://slop-blog.enkhayzomachines.net/posts/penumbra-black-plague-requiem-high-fps-ril) :)

An x86 ASI plugin that decouples presentation from the fixed 60 Hz simulation
in the Windows builds of *Penumbra: Black Plague* and *Penumbra: Requiem*.
Logic and physics keep their native 60 Hz step; the plugin renders at the
display rate and reconstructs presentation state between completed logic
ticks.

It is a port of `PenumbraOverture.FPSFix`. One ASI and one INI serve both
games: both executables live in the same directory, and the plugin identifies
which one loaded it.

**Current version: 1.0.0-rc1, a release candidate.** It has been played in
both games without a failure, but neither game has been played through to the
end with it. See *Status* below.

## How the host is identified

The plugin never trusts the file name, timestamp, or hash. At startup it scans
the mapped main module with two adapters, one per executable:

- 24 masked signatures for the shared HPL1 engine layer, each unique in both
  executables;
- 19 game-layer signatures per executable, 15 of which are *layout witnesses*:
  windows of game code whose literal bytes contain the object offsets the
  plugin reads (player state, cursor positions, UI owner flags, and so on).

An adapter is accepted only if every one of its signatures matches as
required, and the plugin proceeds only if exactly one adapter is accepted.
Anything else, including an unrelated process in the same directory, fails
closed: the reason is logged and the process is left unmodified. Because the
layout values are carried by the signatures, a layout can never be applied to
a binary compiled against different offsets.

The first lines of `PenumbraBlackPlague.FPSFix.log` name the selected adapter.

The Steam executables are a 2009 relink wrapped in SteamStub DRM, so their
files differ from GOG's and `.text` is encrypted on disk. Once the stub has
decrypted it, the same two adapters accept them unchanged: all 43 signatures
resolve, and every function the plugin hooks is instruction-for-instruction
identical to the GOG build at a different address. The log then reports
`reference build=false`, which is expected.

## Status

Counts are from the plugin's own telemetry over about 25 minutes of *Black
Plague* and 12 minutes of *Requiem* play. "Played" means the path ran in a
level with no error, no failed apply or restore, and looked right to the
player.

| Area | *Black Plague* (`Penumbra.exe`) | *Requiem* (`Requiem.exe`) |
| --- | --- | --- |
| Loader through `winmm.dll`, adapter selection | Played | Played |
| Render unlock, logic held at 60 Hz | Played | Played |
| Camera interpolation | Played | Played |
| Mouse look preview and axis correction | Played | Played |
| Hands and held items | Played | Briefly (few held items) |
| World roots (physics objects), including pickup and destruction | Played | Played |
| Skeletal poses and physics-driven skeletons | Played | Played |
| Animated enemy and its mesh root | Played (one enemy encounter) | No enemies in this game |
| Particles, including line emitters | Played | Played |
| Image-trail timing | Played | Not yet seen; off by default |
| Motion-blur timing | Not yet run; off by default | Not yet run; off by default |
| Interaction cursor | Played | Played |
| GUI cursors: main menu, inventory | Played | Played |
| GUI cursors: notebook, death screen | Played | Not yet seen |
| GUI cursor: numerical panel | Not yet seen | Not yet seen |

Still open before a final release: a complete playthrough of each game, the
other enemy types (dog, spider, rock worm), a range of frame rates
with and without VSync, save/load and map-transition cycles, and Alt-Tab.

Not covered by design: the in-game engine-GUI screens (computer terminals)
keep their stock fixed-rate cursor, and mouse look preview is inhibited while
one is open. Beam particle emitters stay at the logic rate. Haptic input does
not exist in these builds.

Two fixes came out of play testing and are worth knowing about when reading
the code: world-root entries are dropped as soon as a node's first word
changes, because the engine destroys nodes without notice (a picked-up item
crashed the first build); and every active particle emitter is regenerated
each frame, because the engine builds quads in the view space of the camera
it is handed.

## Configuration

`PenumbraBlackPlague.FPSFix.ini` sits beside the ASI. `[Features]` applies to
both games. A key placed in `[BlackPlague]` or `[Requiem]` replaces the same
`[Features]` key for that game only, so a feature can be enabled for one game
without the other.

Use VSync or an external limiter to cap the frame rate; `PresentationLimiter`
and `TargetFPS` are reserved and not implemented. Telemetry is off in the
release INI because the log grows by several megabytes per hour; turn it on
when investigating a problem.

## Build

Configure from an x86 Visual Studio developer environment:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The ASI and its INI are written to `build/bin`.

To also test the adapters against an installed copy of the games, point the
build at the directory that holds both executables. The files are mapped
read-only and never executed:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
      "-DPBPFPS_GAME_DIR=<install>\redist"
```

## Packaging

```powershell
cmake --build build --target dist
```

writes two flat archives to `dist/`: `...-plugin-only.zip` and, when the
pinned Ultimate ASI Loader DLL is available at `PBPFPS_LOADER_DLL`,
`...-with-Ultimate-ASI-Loader.zip`. `packaging/package.ps1` refuses any loader
other than the pinned build, and re-opens each archive to verify every entry
against its `SHA256SUMS.txt`. `packaging/README.txt` is the player-facing
readme and carries the install and uninstall instructions.

## Source layout

```text
src/
  target_host.*              identify the host executable, choose the adapter
  target_layout.*            every offset and vtable index, one table per host
  target_engine_layout.h     engine fields shared by both games
  target_game_layout.h       game fields shared by both games
  target_black_plague.cpp    adapter: signatures and layout for Penumbra.exe
  target_requiem.cpp         adapter: signatures and layout for Requiem.exe
  target_*_signatures.inc    generated masked signature tables
  target_mapping.*           signature and import resolution
  *_runtime.*                feature runtimes; no addresses or offsets
  telemetry.*                hook layer and the periodic report
```
