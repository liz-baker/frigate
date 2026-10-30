# Documentation

Start with the [README](../README.md) for what Frigate is, then pick a section.

## Getting started

- [building.md](building.md): dependencies, building and running on each platform, flags, troubleshooting
- [design.md](design.md): the goal, why it is built on libultraship, how assets and fidelity work, source layout

## Using the game

- [menu.md](menu.md): the configuration menu
- [modern-controls.md](modern-controls.md): keyboard-and-mouse and dual-stick play
- [bindings.md](bindings.md): the binding store behind the controls editor
- [multiplayer-quick-start.md](multiplayer-quick-start.md): start a 2 to 4 player game from the command line

## Subsystems

- [extract.md](extract.md): the ROM extraction library and first-run extraction
- [audio.md](audio.md): from ROM sound banks to the speakers, and how to check it
- [multiplayer-split-screen.md](multiplayer-split-screen.md): split-screen layouts and rendering

## Testing

- [testing.md](testing.md): every check, what it catches, when to run it
- [extract-verification.md](extract-verification.md): byte-for-byte parity with the reference packers
- [multiplayer-verification.md](multiplayer-verification.md): the headless harness and the manual checklist

## Binary formats

On-disk layouts of the ROM data and of the O2R resources Frigate packs from
it. Code comments point here rather than repeating them.

**Archive container**
- [o2r-resource-header.md](formats/o2r-resource-header.md): the header every packed resource starts with

**Containers and levels**
- [goldeneye-containers.md](formats/goldeneye-containers.md): model and bg containers
- [goldeneye-stage-setup.md](formats/goldeneye-stage-setup.md): stage setup files
- [goldeneye-stan-resource.md](formats/goldeneye-stan-resource.md): the stan file and `GEST` resource
- [goldeneye-ai-lists.md](formats/goldeneye-ai-lists.md): AI lists
- [goldeneye-monitor-lists.md](formats/goldeneye-monitor-lists.md): monitor animation lists
- [goldeneye-water-lists.md](formats/goldeneye-water-lists.md): water lists
- [goldeneye-briefing.md](formats/goldeneye-briefing.md): briefing files
- [goldeneye-ramrom.md](formats/goldeneye-ramrom.md): attract-mode demos

**Models and animation**
- [goldeneye-model-nodes.md](formats/goldeneye-model-nodes.md): the model node tree
- [goldeneye-model-tree-resource.md](formats/goldeneye-model-tree-resource.md): the `GEMD` resource
- [goldeneye-model-skeleton-resource.md](formats/goldeneye-model-skeleton-resource.md): the `GESK` resource
- [goldeneye-vertex.md](formats/goldeneye-vertex.md): vertex data
- [goldeneye-animation-table.md](formats/goldeneye-animation-table.md): animation tables
- [goldeneye-animation-pose.md](formats/goldeneye-animation-pose.md): the pose bitstream
- [goldeneye-animation-clip-resource.md](formats/goldeneye-animation-clip-resource.md): the `GEAN` resource
- [goldeneye-chr-records.md](formats/goldeneye-chr-records.md): the character model table
- [goldeneye-chr-combat.md](formats/goldeneye-chr-combat.md): characters' combat tables
- [goldeneye-prop-models.md](formats/goldeneye-prop-models.md): the object model table
- [goldeneye-weapon-stats.md](formats/goldeneye-weapon-stats.md): the weapon table

**Graphics**
- [goldeneye-display-list.md](formats/goldeneye-display-list.md): the display-list stream
- [goldeneye-display-list-resource.md](formats/goldeneye-display-list-resource.md): re-emitted display lists
- [goldeneye-texture-bitstream.md](formats/goldeneye-texture-bitstream.md): the texture-bank bitstream
- [goldeneye-texture-mip-chain.md](formats/goldeneye-texture-mip-chain.md): mip chains
- [goldeneye-texture-hits.md](formats/goldeneye-texture-hits.md): texture surface types
- [goldeneye-sky-images.md](formats/goldeneye-sky-images.md): sky and water images
- [goldeneye-hud-images.md](formats/goldeneye-hud-images.md): HUD images
- [goldeneye-eye-intro-resources.md](formats/goldeneye-eye-intro-resources.md): gun barrel screen resources
- [goldeneye-font-text.md](formats/goldeneye-font-text.md): fonts and text

**Audio and saves**
- [goldeneye-audio-bank.md](formats/goldeneye-audio-bank.md): the audio bank and `GEAB` resource
- [goldeneye-music-sequences.md](formats/goldeneye-music-sequences.md): music sequences and the `GEMS` resource
- [goldeneye-eeprom-save.md](formats/goldeneye-eeprom-save.md): the EEPROM save image

## Contributing

[CONTRIBUTING.md](../CONTRIBUTING.md) covers workflow and conventions. A new
doc belongs in the section above that fits it; add its line here in the same
PR. Binary layouts go in `formats/`, not in code comments.
