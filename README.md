# ASTAR Visualizer

An interactive **C++17** weighted graph pathfinding visualizer powered by [`stlastar.h`](https://github.com/justinhj/astar-algorithm-cpp), **SDL3**, and **Cairo**.

![ASTAR Weighted Graph Pathfinder](https://github.com/user-attachments/assets/5f3ca4e4-f437-4569-ab35-62c7dd4f047d)

## Features

- **Real-time A\* Search Animation**: Step-by-step visualization of Open list frontiers, Closed nodes, active edge relaxation, and the final optimal weighted path using `AStarSearch` from `astar-algorithm-cpp`.
- **Interactive Start & Goal Selection**: Left-click any node to set **START**, or Right-click (or `Shift + Left-click`) to set **GOAL**.
- **Configurable Graph Size & Topologies**:
  - **Size slider**: `12–220` nodes.
  - **Layouts**: `RADIAL` (concentric rings), `ORGANIC` (Poisson-disc-like constellation), and `MESH` (warped grid).
- **Weighted Edges**: Edge costs scale Euclidean distance by multipliers (`1.0x–4.2x`), keeping the Euclidean heuristic admissible and consistent while forcing A\* to route around heavy edges.
- **Telemetry HUD & Playback Controls**: Live `GRAPH`, `STEP`, `STATE`, `COST`, and `OPN / CLS / PTH` meters, hover inspection (`g`, `h`, `f`), playback speed slider (`1–40` steps/s), single-step mode, and weight badge toggle.

## Dependencies

- **C++17** compiler & **CMake** (`>= 3.20`)
- **pkg-config**
- **SDL3** (`sdl3`)
- **Cairo** (`cairo`)
- [`astar-algorithm-cpp`](https://github.com/justinhj/astar-algorithm-cpp) (`stlastar.h` and `fsa.h`, fetched automatically by CMake during configure)

On macOS with Homebrew:

```bash
brew install cmake pkg-config sdl3 cairo
```

## Build & Run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

CMake automatically downloads `stlastar.h` and `fsa.h` from GitHub. To use a local checkout instead, pass `-DASTAR_CPP_DIR=/path/to/astar-algorithm-cpp` during configuration.

Run the visualizer:

```bash
./build/astar_visualizer
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

## Controls

| Input | Action |
| --- | --- |
| **Left-click node** | Set **START** node |
| **Right-click / Shift+Click node** | Set **GOAL** node |
| `Space` | Play / Pause animation |
| `S` | Advance a single A\* step |
| `R` | Generate a new graph |
| `C` | Reset current search |
| `W` | Toggle edge weight badges |
| `1` / `2` / `3` | Switch layout (`RADIAL` / `ORGANIC` / `MESH`) |
| `Esc` | Quit |

## CLI Options

- `--nodes <N>` — Initial node count (`12–220`, default `64`)
- `--layout <0|1|2>` — Initial layout (`0`: Radial, `1`: Organic, `2`: Mesh)
- `--headless-demo <output.png>` — Render a headless preview PNG and exit
- `--steps <N>` — Max search steps for `--headless-demo` (`-1` runs to completion)
