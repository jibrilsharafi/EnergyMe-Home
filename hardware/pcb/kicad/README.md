# KiCad projects (derived from the EasyEDA backups)

One KiCad 10 project per board, generated from `../backup_easy_eda/*.json` by
the `kicad3d-easyeda-import` tool of the `energyme-3d` repo and used by it to
build the 3D models (GLB for the web, STEP for mechanical CAD, the assembled
device model).

| Board | Source | Contents |
|---|---|---|
| `main_board/` | `pcb_main_board.json` | 87 footprints, 50 x 87 mm, 8 slots + the DIN clip notch |
| `top_board_1/` | `pcb_top_board_1.json` | CH2/CH3 on header H1, stacks on the main board's H2 |
| `top_board_2/` | `pcb_top_board_2.json` | CH10..CH15 on header H4, stacks on the main board's H3 |

`EASYEDA_MODELS/` next to each board holds the LCSC 3D models (`.wrl` for
KiCad's renderer, `.step` twin for the STEP export) fetched with
easyeda2kicad. They are committed because the EasyEDA API rate-limits and
drops parts over time; U2/U4 (ZX-QC66 buttons) have no model at all.

Regenerate after an EasyEDA change (from the `energyme-3d` checkout):

```bash
uv run kicad3d-easyeda-import --json <this repo>/hardware/pcb/backup_easy_eda/pcb_main_board.json --out <this repo>/hardware/pcb/kicad/main_board/main_board.kicad_pcb
uv run kicad3d-attach-models --board <this repo>/hardware/pcb/kicad/main_board/main_board.kicad_pcb --bom <this repo>/hardware/pcb/main_board/BOM_*.csv
```

The import rewrites Edge.Cuts as outer contour minus the union of the EasyEDA
cutouts; without that KiCad's exporters drop every slot.
