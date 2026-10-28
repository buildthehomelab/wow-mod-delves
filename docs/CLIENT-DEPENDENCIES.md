# Converted client reference audit

`tools/check_client_dependencies.py` parses converted 3.3.5 files independently
of the retail converter. It inventories names case-insensitively and checks:

- ADT/WDT texture, doodad and WMO filename tables, plus active WDT terrain tiles.
- WMO texture, doodad and skybox filename tables and derived numbered group files.
- MD20 version264 named textures, numbered skin companions, external animation
  companions and particle geometry/recursion model filenames. Replaceable texture
  types are reported separately rather than silently assumed resolved.

It rejects truncated/out-of-range tables, unknown converted versions, unsafe paths,
case-insensitive duplicate files and missing referenced files. Repeated ADT MCNK
terrain cells are valid; this audit does not decode their geometry. The source
format facts were checked against the existing pinned converter's MD20 header,
sequence naming/flags and WMO root definitions; its implementation is not imported.

```sh
python3 tools/check_client_dependencies.py \
  --client /path/to/client-registered-candidate \
  --output /tmp/earthcrawl-dependencies.json
```

The checked candidate has 809 files, including its local metadata manifest:
193 M2s, one WDT, six ADTs and26 WMOs were parsed. All1,199 named/derived file
references resolve within the candidate; there are zero parse errors and zero
replaceable texture bindings. `test-package/client-dependencies.candidate.json`
records the content-inventory digest, counts and explicit validation limits.
The inventory digest hashes sorted normalized paths and each file's SHA256;
it identifies these inputs, without shipping any game binaries in this repository.

This narrows the missing-file risk; it does not certify client rendering or a
complete gameplay dependency closure. Texture decoding, skin/animation tracks,
materials/shaders, lighting, liquids, sound, spell/DBC effects and actual client
execution need their respective checks. The separate asset checker pairs the
candidate MPQ hash and native geometry/DBC metadata. Neither tool establishes
current realm/client baseline, patch ordering, ID availability or installation.
Do not replace a server/client file on the strength of this report alone.

Synthetic tests cover missing companions, animation names, WMO groups/WDT tile
references, repeated terrain chunks, unsafe paths and truncated/out-of-range data.
No server or client was started and no game data installed during this audit.
