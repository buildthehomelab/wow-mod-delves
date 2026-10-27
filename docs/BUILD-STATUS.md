# Pilot build status — October 27, 2026

This repository is a run-state foundation. It does not yet register an AzerothCore
module or implement the Earthcrawl encounters. It must not be installed as a module.

The separate Evermore workspace asset experiment has reached these checks:

- Source retail map 2680, build 12.1.5.70575, build configuration
  `d718abcc42604d8efbd35aecfa5bb75b`; hashes checked against CDN content keys.
- 819 dependency files discovered (270,555,255 bytes); zero unresolved references
  in the implemented dependency traversal. Lighting, sound, liquid data and game
  database dependencies are outside that traversal.
- Conversion by Bar3b0n3s/Converter revision
  `d71523c645ebe1313e15fe47fbea57577adf8c80`. Conversion reports visual/geometry
  losses, including material/shader substitutions and terrain hole precision.
- Geometry archive: 804 game files, reopened and hash-compared individually.
- 1,193 checked named dependencies resolve; 1,820,334 WMO vertex indices in bounds.
- Native Evermore core revision `9e4a232a84d375a8906717c5139fb8d6bf2e41d6`
  successfully extracts six terrain tiles and assembles collision data.
- With provisional runtime map ID 890, its navigation generator produces four
  occupied tiles, which native Detour loads with 14,994 polygons. This is structural
  validation, not a tested player or companion route.

Retail map ID 2680 exposed a filename buffer overflow in the current generator
(`MapBuilder::discoverTiles`, `char filter[12]` and the four-digit map wildcard).
The source asset directory remains `world/maps/2680/`; the offline fixture's Map
row uses runtime ID 890. This ID is absent from the local client snapshot, but
has not been reserved against the current deployed realm. A production patch
must preserve the realm's current DBC rows and resolve ID ownership first.

The source build's map also includes a later-season arena, so these asset checks
must not be described as a historical Season 1 map reconstruction. Season 1 is
still the intended pilot gameplay reference. A complete compatible client DBC
patch, spawn catalog, companion, reward integration and in-game validation remain.

Araxia's MIT-licensed custom delve source is a second content stream to evaluate,
not a substitute for the retail pilot. The inspected source revision is
`aaea411c6ea1831e19e284ed4e4445f83f796272`; its README lists thirteen locations,
including WIP entries. Many are designed for levels 82–85. No upstream SQL has
been applied and no encounter behavior has been validated on Evermore.

No game assets are included in this repository. No server was rebuilt, restarted,
reconfigured or given new database content by these offline experiments.
