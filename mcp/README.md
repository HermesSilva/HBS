# mcp — .mass model-editing MCP server

Pure C++17 model-editing library + MCP server over the BidirectionalGaitNet
`.mass` project. No DART, no Python — only nlohmann/json, tinyxml2 (`.osim` atlas)
and Boost.Asio (TCP transport). Copied/adapted from MASS-Easy's `libmassedit` and
re-targeted to this project's `.mass` schema (adds the GaitNet `env.xml` config,
the `<parameter>` block and per-joint `kp`/`kv`, which round-trip losslessly).

## Modules
| File | Role |
|---|---|
| `MassModel.{h,cpp}` | `Model` struct + `.mass` JSON IO + stable `uid` (`assignUids`) |
| `Index.{h,cpp}` | generational-handle ids, reverse indices, group selector |
| `Query.{h,cpp}` | JSON facade for the read tools |
| `Kinematics.*` | FK: rotate joint, propagate subtree, re-anchor waypoints |
| `DofMap.*` | anatomical DOF layer (name → joint/axis/sign/range) |
| `Batch.*` | `scale_bone`, `translate_subtree`, L/R symmetric |
| `Complete.*` | finger/phalanx generation, `list_gaps`, symmetric fill |
| `Atlas.*` | OpenSim `.osim` parse, normalized join, `validate`, `sync` |
| `Groom.*` | hair groom params + PBD guide solver |
| `Mcp.*` | MCP JSON-RPC dispatch (`McpServer`) + single-writer queue (`McpQueue`) |

## Tools (`tools/call`)
`describe_model`, `get_node`, `get_muscle`, `select`, `muscles_of_body`,
`muscles_crossing_joint`, `scale_bone`, `translate_subtree`, `rotate_joint`,
`generate_fingers`, `list_gaps`, `list_inert_muscles`, `add_muscles`,
`reanchor_waypoints`, `set_muscle`, `load_atlas`, `validate_anatomy`,
`sync_from_atlas`, `save`, `load`. Mutating tools rebuild the index.

### Anatomy workflow
The atlas is external ground truth, assembled from several OpenSim models since
none covers the whole body. Load them and the synonym table that joins their
abbreviated names (`glut_max1_r`) to this model's spelled-out bundles
(`Gluteus_Maximus`, `Gluteus_Maximus1`) — without it the name join matches
almost nothing:

```
load_atlas   {"path":"data/atlas/gait2392_thelen2003muscle.osim"}   # lower limb + trunk
load_atlas   {"path":"data/atlas/arm26.osim"}                       # upper arm
load_atlas   {"path":"data/atlas/wrist.osim"}                       # forearm/wrist
load_atlas   {"synonyms":"data/atlas/synonyms.json"}
validate_anatomy
sync_from_atlas {"fillHill":true}
```

`sync_from_atlas` copies the pennation angle, spreads each atlas muscle's f0
over the model bundles that share it, and derives `pcsa_cm2`. It leaves `lm`/`lt`
alone unless `fillLengths` is set: OpenSim stores those in metres and MASS as
fractions of the rest muscle-tendon length, so they need converting, and
rewriting them invalidates a trained policy.

`add_muscles {"path":"data/atlas/missing_muscles.json"}` adds the muscles the
model lacks, mirroring each left-side definition to the right.

## Build & run
Built by the top-level CMake (`GAITNET_BUILD_MCP=ON`) → `build/mcp/<Config>/gaitnet-mcp.exe`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
powershell -ExecutionPolicy Bypass -File scripts\mcp.ps1 data\project.mass 8766
```

The server speaks newline-delimited JSON-RPC over TCP:
`initialize`, `tools/list`, `tools/call`. Example:
`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"describe_model","arguments":{}}}`

## Two ways to run it

**Standalone** — `gaitnet-mcp <model.mass> [port]` (default 8766) owns a model
loaded from disk. Good for batch edits, e.g. `scripts/build_model02.ps1`.

**Inside the Arena editor** — the editor links `massedit` and serves the same
tools on **127.0.0.1:8767**, operating on the model that is open on screen, so an
edit is visible immediately:

```powershell
& scripts\mcp_call.ps1 -Port 8767 -Call "describe_model"
```

Requests are parked on `McpQueue` by the network thread and applied by the UI
thread in `App::mcpPoll`, one drain per frame — the model has a single writer and
needs no locking. The `McpServer` lives across frames, so an atlas loaded by one
call is still there for the next.

Because the editor and `massedit` hold the same data in two separate `Model`
types, each drain converts one into the other through the `.mass` JSON they both
already agree on (`Model::ToJsonString` / `FromJsonString`).

## In-process Arena bridge (implementation notes)
`McpQueue` is the co-edit mechanism: any thread `submit`s a request and gets a
`std::future`; the owner (Arena's UI thread) `drain`s once per frame, applying
every request through `McpServer` on that single thread — no model locks. Wiring
this into the Arena editor (Asio transport → queue → drain in `App::frame`) lets
an AI edit the live model while it is open. Not yet wired here.
