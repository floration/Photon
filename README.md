# Photon

A house fire simulator for firefighter training, written in raw C++20 with OpenGL. No engine and no framework, and no third-party libraries.

**Status: Phase 2 (foundations) on top of the Phase 1 prototype. Windows only for now.** It uses prototype physics and is not validated. The fire behaves roughly right in shape and wrong in numbers. Use it for training and education only. Read [docs/PROTOTYPE_PHYSICS.md](docs/PROTOTYPE_PHYSICS.md) for what is simplified.

## What Phase 1 does

- The default model home is now a 1.5-story Cape Cod (40 x 28 ft, 12/12 stick-framed roof, 5 ft knee walls, sloped ceilings, knee-wall attics). It is furnished from the asset library in data/assets.txt. The earlier two-story house is in scenes/two_story.txt. (Phase 1 originally used a 50 x 30 ft single-story ranch with 2 bedrooms and 1 bath. It has a slab foundation, 2x4 walls at 16 in o.c., Fink trusses at 24 in o.c., a vented attic, double-pane windows and furniture. See `scenes/model_home.txt`.
- The scene is exact. Every stud, plate, header, truss chord, web and gusset plate is generated at its actual size. The solver works on a derived 20 cm grid of about 160,000 cells.
- Fire and smoke engine, run on the CPU across all cores:
  - Buoyant gas flow with a pressure projection.
  - Gas expansion driven by heat release.
  - Open atmospheric boundaries.
  - Transport of temperature, oxygen, fuel gas, black soot and light-colored pyrolysis aerosol.
- Solids never burn directly. They heat up, give off fuel gas and lose mass, and only that gas burns. Burned-out furniture is removed from the grid.
- Walls, ceilings, roof, doors and windows are layered barriers. They heat through and fail layer by layer: glass falls out, gypsum comes down, and doors and roof decking burn through. Each failure opens a new flow path.
- The viewer shows:
  - Smoke as a ray-marched volume. Its color comes from the computed mix of soot and aerosol.
  - Flame drawn from the computed heat release.
  - Hot surfaces that glow by their temperature.
  - Cutaway toggles, a framing view and a temperature slice.
  - An event log ("Living south window failed", "Ceiling opened over Living - fire into attic").

## Phase 2 additions

- **Object-based house.** The scene expands into about 3,000 physical objects: every framing member, standard 4 x 8 ft gypsum and OSB sheets, subfloor and roof-deck panels, shingle fields, door slabs, window units, floor coverings and furniture parts, each at its true size. Walls are drawn with real thickness at their true layer positions, and the roof as a true slope.
- **UI.** A scene panel (G) with click action, ignition source, simulation controls, view toggles, doors and windows, and rooms. An object inspector (Ctrl+click) shows dimensions, the material properties and their citation, and the live state.
- **Background runs.** "Queue this fire as a background run" writes `jobs/runN.job`. A separate low-priority `photon_cli --job` process runs it on half the cores, with pause, resume and cancel from the panel. Results go to `runN.job.csv` and `runN.job.events.txt`. Nothing heavy starts unless you queue it.
- **Versioned data formats.** `data/materials.txt`, `assemblies.txt`, `components.txt` and scene files all start with a format version, and newer versions are refused with a clear message. The component library holds actual standard sizes with citations.
- **Tests.** `build\photon_tests.exe` runs in about 2 s. It covers the formats, scene and objects, grid invariants, smoke containment, determinism and the burner.

## Build (Windows)

You need CMake 3.20 or newer and a C++20 compiler (MinGW-w64 GCC or MSVC).

```
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j
build\photon.exe
```

The Linux platform layer arrives in Phase 2.

`photon_cli` runs the solver with no window, which shows the solver library builds without the platform or renderer:

```
build\photon_cli.exe scenes/model_home.txt 300 Sofa 10
```

The arguments are the scene, the seconds to simulate, the item to ignite and the source power in kW.

## Controls

| Input | Action |
| --- | --- |
| Left-click a surface | Place the selected ignition source there. The solver decides whether anything ignites. |
| 1 / 2 / 3 | Source: match flame (0.05 kW), small flame (10 kW), large flame (100 kW) |
| Right-click a door or window | Open or close the door, or break the window |
| W A S D | Fly forward, left, back, right |
| Space / C (or E / Q) | Fly up / down |
| Shift / Ctrl | Fly faster / slower |
| Hold a mouse button and drag, or arrow keys | Look around |
| Wheel | Dolly forward or back |
| P | Pause and resume |
| + / - | Change the simulation speed |
| R / Z / F | Hide the roof, hide the ceiling, framing view (also F2 / F3 / F4) |
| X / V | Smoke on or off, fire on or off |
| T, PgUp / PgDn | Temperature slice, slice height |
| F12 | Save a screenshot next to the exe |
| Backspace | Reset the house |
| H | Show or hide the help |

## Layout

| Path | Contents |
| --- | --- |
| `src/core` | Job system and math |
| `src/scene` | Text formats, material library, layered assemblies, scene and framing generation |
| `src/sim` | Discretizer and solver. This library has no platform or renderer dependency. |
| `src/platform` | Win32 window, WGL context, input and font rasterization |
| `src/render` | Hand-written OpenGL loader and the renderer |
| `src/app` | Viewer and headless CLI |
| `data/` | Materials and assemblies. Every value carries a source line. |
| `scenes/` | Scene files, human-readable |
| `docs/` | Prototype physics notes and the data register |
