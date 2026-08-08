# Changelog

All notable changes made in HBS (Human Body Simulator) relative to the upstream
[namjohn10/BidirectionalGaitNet](https://github.com/namjohn10/BidirectionalGaitNet)
are documented here. Upstream code, models and data remain the work of the original
authors (Jungnam Park, Moon Seok Park, Jehee Lee, Jungdam Won).

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased] — Model-editing MCP server (`mcp/`)

Copied MASS-Easy's `libmassedit` MCP into this project and re-targeted it to the
BidirectionalGaitNet `.mass` schema, so an AI/MCP client can query and edit the
musculoskeletal model programmatically. Pure C++17 (no DART, no Python): only
nlohmann/json, tinyxml2 (`.osim` atlas) and Boost.Asio (TCP transport).

### Added
- `mcp/` — the model-editing library + a standalone MCP server
  (`Dist/x64/<Config>/gaitnet-mcp.exe`). `scripts/mcp.ps1` launches it.
- Modules: MassModel, Index (generational handles + reverse indices + group
  selector), Query, Kinematics (FK), DofMap, Batch (scale_bone/translate_subtree),
  Complete (finger generation), Atlas (.osim), Groom (hair PBD), Mcp (JSON-RPC
  dispatch + single-writer `McpQueue`).
- Tools: `describe_model`, `get_node`, `get_muscle`, `select`, `muscles_of_body`,
  `muscles_crossing_joint`, `scale_bone`, `translate_subtree`, `rotate_joint`,
  `generate_fingers`, `list_gaps`, `load_atlas`, `validate_anatomy`,
  `sync_from_atlas`, `save`, `load`.
- Top-level CMake option `GAITNET_BUILD_MCP` (ON).

### Changed
- `mcp/MassModel`: merged this project's GaitNet extensions (per-joint `kp`/`kv`,
  the `EnvConfig` env.xml block, the `<parameter>` block) into the model + JSON so
  the MCP round-trips a `.mass` produced by the editor without dropping data.

### Verified
- `gaitnet-mcp data/project.mass 8766` loads (23 bones, 304 muscles); a TCP client
  gets valid `initialize`, `tools/list` and `tools/call describe_model` responses.

### In-process Arena bridge — now wired
- `mcp/` builds a `massedit` static library, which the editor links; the
  standalone server is a thin `main.cpp` on top of it.
- `Arena/src/McpHost.{h,cpp}`: an Asio acceptor on **127.0.0.1:8767** parks each
  request on `McpQueue`; `App::mcpPoll` drains it once per frame, so the model
  keeps a single writer and needs no locking. The `McpServer` instance lives
  across frames, so a loaded atlas survives between calls.
- The editor's `Model` and libmassedit's `Model` are separate types holding the
  same data, so a request converts one to the other through the `.mass` JSON both
  already agree on (`Model::ToJsonString` / `FromJsonString`, added to both).

## [Unreleased] — renamed: the project is HBS, the editor is Arena

### Changed
- The project is **HBS — Human Body Simulator**: `BGN.slnx` → `HBS.slnx`,
  `Bgn.props` → `Hbs.props`, and the docs name it accordingly. The fork notice
  and every credit to the upstream authors are untouched: HBS names this fork's
  own work, not the research it stands on.
- `editor/` → `Arena/`, and the CMake target `editor` → `Arena`, so the binary
  is `Arena.exe` — the name `Arena.vcxproj` already produced. `scripts/Arena.ps1`,
  `build.cmd`, `HBS.slnx`, `.gitignore` and the docs follow.
- `Arena/Arena.vcxproj` had fallen behind the CMake build: it was missing
  `McpHost.cpp` and the libmassedit sources Arena now needs. Both paths compile
  the same set again.

### Note
- `bgn/` and `fgn/` keep their names. They are the **Backward** and **Forward
  GaitNet** of the paper, not the project's name, and the checkpoints inside are
  named after them.

## [Unreleased] — build.cmd, and every executable in Dist/x64/<Config>

### Added
- `build.cmd` — one-shot build of everything from cmd.exe: configure (reusing
  the sibling MASS vcpkg) plus all targets. `debug`, `fresh`, `noviewer` and
  `target <name>` combine; `VCPKG_ROOT` and `CMAKE_GENERATOR` override the
  defaults. Distinct exit codes per failure (missing cmake, missing toolchain,
  configure, build) so it composes in a pipeline.

### Changed
- Executables now land in `Dist/x64/<Config>/`, the layout `Arena.vcxproj`
  already used, instead of one directory per target under `build/`. Set through
  the per-config `CMAKE_RUNTIME_OUTPUT_DIRECTORY_<CONFIG>` variables — the plain
  one would make a multi-config generator append a second `<Config>` level — and
  the per-target overrides were dropped. `scripts/Arena.ps1`, `mcp.ps1`,
  `view.ps1` and `build_model02.ps1` follow.

## [Unreleased] — Arena: stripped UI, in-process MCP, versioned training sets

### Changed
- **UI reduced to the button bar.** Every docked panel is gone (Scene, Properties,
  Tools, GaitNet, Anatomy check, Atlas, Training, Fills, Lights) along with the
  dockspace; the window is now the menu bar, the icon row and a full-bleed 3D
  view. The model is driven through the MCP server instead of through panels.
- `deriveRoot` no longer returns the literal `"data"` for a one-component
  relative path (`data/x.mass`), which made every derived path `data/data/...`.

### Added
- **Training sets versioned by muscle structure** (`data/train/m<N>_<sig>/`):
  env.xml + skeleton + muscle xml plus a `manifest.json` recording the muscle and
  bone counts and the network shape the set requires. `App::muscleSignature`
  hashes what decides that shape — which muscles exist, in what order, and which
  bones each pulls on — and deliberately ignores Hill values and waypoint
  coordinates, since retuning a force does not invalidate a checkpoint but adding
  or rerouting a muscle does. A new set is written automatically (debounced)
  whenever the structure settles on something new; the button bar has a manual
  export and a toggle for the automatic one.

### Removed
- `App::startTraining`, which had no caller left after the Training panel went
  away and launched `scripts/train.ps1` — a script that does not exist in this
  port, where offline training was removed.

## [Unreleased] — Visual character editor (`editor/`, since renamed `Arena/`)

Added a standalone Arena-style 3D character editor that edits this project's native
formats. It is self-contained: it links this repo's `sim` library and vcpkg
packages only — there is no source/link dependency on the sibling MASS project
(the editor is adapted from MASS-Easy's "Arena" editor, re-targeted to the
BidirectionalGaitNet `env.xml` format).

### Added
- `editor/` — GLFW + Dear ImGui (docking) + ImGuizmo + OpenGL editor (binary
  `editor.exe`). Namespace `ed`. `scripts/editor.ps1` launches it.
- 3D viewport: bones from the model's OBJ meshes, 304 muscles as fusiform tubes,
  ImGuizmo move/rotate/scale, mesh picking, undo/redo, scene tree + properties.
- **Unified `.mass` JSON project** grouping skeleton + muscles + motions + the
  `env.xml` settings + the `<parameter>` block + scene lights/fills. Import a
  project from `env.xml` (File ▸ Import env.xml) and export back to
  `env.xml` + `skeleton_gaitnet_narrow_model.xml` + `muscle_gaitnet.xml`.
- **GaitNet panel**: edits the `env.xml` master config (actuator, defaultKp/Kv,
  Hz, actionScale, reward type/weights, flags) and the `<parameter>` ranges
  (gait / skeleton / torsion / muscle_length / muscle_force) with add/remove.
- **Live simulation** on this repo's `sim::Environment` (a worker thread writes a
  temp `env.xml` + skeleton + muscle, calls `Environment::initialize`, then plays
  the reference motion (kinematic) or steps the DART sim (dynamic)).
- Extras carried over from Arena: procedural skin/fills (metaballs + marching
  cubes), OpenSim `.osim` muscle atlas import, Boost.Asio training-telemetry bridge.

### Format work
- `Model` extended for GaitNet: per-joint `kp`/`kv` (Ball 3-vector / Revolute
  scalar), full `EnvConfig`, and the `<parameter>` block — all round-trip through
  the `.mass` JSON and the native xml (verified: 23 nodes, 304 muscles,
  kp/kv preserved, env.xml faithfully reproduced).
- Bootstrap rewritten from the MASS `metadata.txt` triplet to this project's
  `env.xml` using `tinyxml2` (the sim library's XML dep) instead of `tinyxml`.

## [Unreleased] — Python-free inference (pure C++/Eigen)

Removed the runtime dependency on Python/PyTorch entirely. The simulation and the
viewer now evaluate every neural network in C++ (Eigen); the app runs with no
Python interpreter, no `pybind11`, and no `torch` on the machine. The project
targets real-time inference with the shipped pre-trained networks, so the offline
training code (which is what needed Python) was dropped.

### Added
- `sim/NN.{h,cpp}` — pure-Eigen inference for all networks: `MuscleNN`
  (two-level muscle control), `PolicyNN` (SimulationNN actor + `weight_filter`),
  `RefNN` (Forward GaitNet) and `GaitVAE` (Backward GaitNet), plus a minimal
  `safetensors` reader (JSON header via nlohmann_json + f32 blobs).
- `tools/export_weights.py` — one-time migration tool. Converts the shipped
  pickled ray/torch checkpoints (`fgn/…`, `bgn/…`, `data/trained_nn/…`) into
  `.safetensors` the C++ loads at runtime. The ray "worker" blob is read without
  ray installed via a permissive stub Unpickler. The converted `.safetensors`
  are committed alongside the originals.
- Committed `*.safetensors` for fgn, bgn and the four cascading policies.

### Changed
- `sim/Environment.{h,cpp}`, `sim/Character.h`: dropped the embedded Python
  interpreter; `Network::joint/muscle` and `mMuscleNN` are now `nn::*` objects;
  muscle forward, cascading `get_action`/`weight_filter` and metadata loading call
  the C++ NN. `setMuscleNetwork` takes an `nn::MuscleNN*`.
- `viewer/`: `get_action`, `loading_network`/`loading_metadata`, Forward/Backward
  GaitNet load + `render_forward` all go through `sim/NN`. `viewer/main.cpp` no
  longer starts a `scoped_interpreter`.
- Build: `sim` links `nlohmann_json` instead of `pybind11::embed`; `viewer` no
  longer links pybind11; the `python/` pysim module was removed from the build.

### Verified
- C++/Eigen forward passes match the original torch modules to float precision
  (max abs diff ≈ 1e-6 for MuscleNN, PolicyNN and RefNN).
- Viewer runs and renders with `python310.dll` deleted and no `PYTHONPATH`.

### Removed
- The entire `python/` package (pysim binding `RayEnvManager.cpp`, PPO/GaitNet
  training scripts, ray glue, SLURM launchers). Model creation now requires
  re-introducing a training path; running the shipped models does not.

### Not yet ported
- **C3D import** (`viewer/C3D_Reader.cpp`) used the Python `c3dTobvh` loader and is
  disabled (`#if 0`); a C++ C3D reader is needed to re-enable it.
- Loading pre-rendered `.npz` motion sets in the viewer (a training-data UI) was
  removed; motions can still be captured live via "Add Current Simulation motion".

## [Earlier] — Native Windows port (MSVC + vcpkg)

Ported the project from its Linux-only toolchain (GCC, EGL, ray/rllib 1.8, Python 3.6)
to build and run natively on Windows with Visual Studio 2026 (MSVC 14.51), CMake and
vcpkg, on Python 3.10. Simulation core, the `pysim` Python binding, and the
OpenGL/ImGui viewer build and run; the Bidirectional GaitNet training/data pipeline
runs on Python 3.10. The Generative GaitNet PPO trainer is deferred (see below).

### Added
- `scripts/build.ps1` — configure + build (sim, pysim, viewer) via MSVC + vcpkg;
  flags `-NoViewer`, `-Fresh`, `-Config`.
- `scripts/view.ps1` — launch the viewer with the correct working directory, runtime
  DLLs and environment.
- `README-Windows.md` — Windows build/run guide and full change list.
- `CHANGELOG.md` — this file.
- Fork/attribution + license notice in `README.md`.

### Changed — build system
- `CMakeLists.txt` (root, `sim`, `python`, `libs`, `viewer`): `cmake_minimum_required`
  bumped to 3.16, C++17, MSVC guards (`/bigobj`, `/permissive-`, `NOMINMAX`,
  `_USE_MATH_DEFINES`, `_CRT_SECURE_NO_WARNINGS`), GCC-only flags (`-fPIC`,
  `-fvisibility=hidden`, `stdc++fs`) guarded behind `if(NOT MSVC)`.
- vcpkg CONFIG targets: `dart`, `dart-gui`, `dart-collision-bullet`, `dart-utils`,
  `tinyxml2::tinyxml2`, `pybind11::embed`, `glfw`, `FreeGLUT::freeglut`,
  `OpenGL::GL/GLU`, `assimp::assimp`. `pysim` built with `pybind11_add_module`.
- Viewer/GLUT/EGL gated behind the new `GAITNET_BUILD_VIEWER` option (off for
  `SERVER_BUILD`).
- `libs/CMakeLists.txt`: dropped the Linux-only EGL / `glad_egl` requirement; imgui
  built with `IMGUI_IMPL_OPENGL_LOADER_GLAD`.

### Changed — C++ sources
- `sim/DARTHelper.{h,cpp}`: `std::experimental::filesystem` → `std::filesystem`; OBJ
  meshes loaded via `MeshShape::loadMesh(Uri::createFromPath(path), LocalResourceRetriever)`
  rooted at `MASS_ROOT_DIR`, so Windows drive letters are not mis-parsed as a URI scheme.
- `sim/Muscle.cpp`: added `<numeric>` / `<algorithm>` (MSVC does not include them transitively).
- `sim/Character.cpp`: replaced `dart::math::clip(vec, Zero, Ones)` (Eigen expression
  template deduction failed on MSVC) with `cwiseMax(0).cwiseMin(1)`.
- `viewer/GLFWApp.{h,cpp}`, `viewer/GLfunctions.cpp`: include `<windows.h>` before GL
  headers (APIENTRY/WINGDIAPI); use glad in place of `<GL/gl.h>`; screenshots via
  `stb_image_write` instead of DART's bundled lodepng; guard `directory_iterator` on
  optional folders (`fgn`, `bgn`, `c3d`, `motions`).
- `viewer/main.cpp`: keep the pybind11 `scoped_interpreter` alive across the top-level
  `try`/`catch` (destroying it during unwinding crashed the handler in
  `PyGILState_Ensure`); inject `MASS_ROOT_DIR/python` and the build output dir into
  `sys.path`.

### Changed — Python
- `import pickle5 as pickle` → `try: import pickle5 … except ImportError: import pickle`
  across all scripts (pickle5 is a Python 3.6/3.7 backport; stdlib `pickle` on 3.8+).
- `ray_model.py`: ray/rllib made **optional** — a lightweight `TorchModelV2` stand-in
  and a local `convert_to_torch_tensor` are used when ray is not installed, so the
  embedded interpreter, the viewer and the GaitNet training import without ray.
- `forward_gaitnet.py`, `advanced_vae.py`, `train_forward_gaitnet.py`,
  `train_backward_gaitnet.py`: import `convert_to_torch_tensor` from the `ray_model`
  shim instead of `ray.rllib.utils.torch_ops`.
- `train_backward_gaitnet.py`: removed `from symbol import parameters` (the `symbol`
  module was removed in Python 3.10 and the import was unused).
- `advanced_vae.py`: `umap` / `matplotlib` imported lazily (only inside the plotting
  method) so training does not require the heavy visualization stack.

### Deferred
- Generative GaitNet **PPO training** (`ray_train.py`, `ray_ppo.py`,
  `ray_torch_policy.py`, `ray_env.py`) still targets the ray/rllib **1.8** execution-plan
  API (`build_trainer`, `with_common_config`, `ParallelRollouts`, `TrainOneStep`,
  `StandardMetricsReporting`, `build_policy_class`, `ray.util.iter.LocalIterator`,
  `import gym`), all removed in ray 2.x. Running it requires either a legacy
  Python 3.8 + ray 1.8 environment or a rewrite against ray 2.x (Algorithm/Learner/
  RLModule + gymnasium).
