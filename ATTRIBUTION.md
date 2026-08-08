# Attribution and third-party terms

HBS stands on published research models and anatomical data. None of it is
redistributed in this repository: the scripts below fetch each source from its
own home, and the derived files they produce are untracked. This keeps the
repository to its own code and makes each source's terms apply where they
should — at the source.

## Upstream project

**Bidirectional GaitNet** (SIGGRAPH 2023) — Jungnam Park, Moon Seok Park,
Jehee Lee, Jungdam Won. <https://github.com/namjohn10/BidirectionalGaitNet>

HBS is a fork of it. The simulation core, the shipped networks and the original
data are the authors' work. **The upstream ships no explicit licence**, so
copyright remains with them and this fork grants no additional rights over that
code. For uses beyond research or academic work, contact the authors
(jungnam04@imo.snu.ac.kr) and cite the paper.

## Reference models and anatomical data

The full survey of sources considered for each layer of the body — naming,
bone geometry and mechanics, muscle lines of action and Hill parameters,
tendons, fat, organs, skin and hair — with the licence and what each one
actually delivers, is kept in [`Docs/fontes_anatomia.csv`](Docs/fontes_anatomia.csv).
Read it before adding a source: several carry terms that would restrict the
project (USC-HairSalon and TotalSegmentator's tissue_types are non-commercial;
AnyBody AMMR and IT'IS Virtual Population are commercial with free academic
tiers).

The sources actually in use today:

| Source | Used for | Terms | Fetched by |
|---|---|---|---|
| [opensim-models](https://github.com/opensim-org/opensim-models) — OpenSim / SimTK | muscle atlas (Hill parameters, attachments) and the per-bone meshes | **no licence declared**; widely redistributed for research. Individual models carry their own papers: Delp et al. 1990 (gait2392), Rajagopal et al. 2016 / Lai-Uhlrich 2023, Hamner et al. 2010, Holzbaur et al. 2005 (arm26), Gonzalez et al. (wrist) | `scripts/fetch_atlas.ps1`, `scripts/fetch_bones.ps1` |
| [MyoSkeleton / myo_sim](https://github.com/MyoHub/myo_sim) — MyoHub | joints, limits, inertias and muscle set for the articulated skeleton | Apache-2.0 | — |
| [BodyParts3D](https://lifesciencedb.jp/bp3d/) — Database Center for Life Science (DBCLS), Japan | per-bone geometry, position and Terminologia Anatomica / FMA naming | **CC BY-SA 2.1 Japan** — attribution required, derivative works share alike | — |

### What CC BY-SA means here

BodyParts3D's licence is *not* a non-commercial one — it permits commercial use.
What it requires is attribution and that derivative works carry the same
licence. The obligation is triggered by **distribution**, not by charging money:
using the data locally asks nothing, publishing a work derived from it does.

This is why BodyParts3D meshes, like every other third-party dataset here, are
downloaded rather than committed. A model file that embedded that geometry would
be a derivative work and would have to be CC BY-SA.

## This fork's own work

Everything written for HBS — the Arena editor (`Arena/`), the model-editing
library and MCP server (`mcp/`), the tools (`tools/`), the scripts (`scripts/`)
and the anatomical data authored here (`data/atlas/*.json`) — is licensed under
**CC BY-SA 4.0**, see [`LICENSE`](LICENSE).

That covers only what this fork added. It does not, and cannot, license the
upstream code or any third-party data listed above.
