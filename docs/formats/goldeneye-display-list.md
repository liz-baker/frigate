# GoldenEye display-list stream

Read by `tools/o2r_packer/ge_displaylist.py`. Big-endian, fixed 8-byte
commands (`w0`, `w1`); the opcode is the top byte of `w0`. Numbering is
the original F3D scheme plus two Rare extensions, matching
`ucode_indy_ge` in libultraship.

| Opcode | Name | Fields |
|---|---|---|
| `0x01` | `G_MTX` | `w1` segment address of the matrix |
| `0x03` | `G_MOVEMEM` | `w1` segment address of the data |
| `0x04` | `G_VTX` | `w0[16..24)` first vertex-buffer slot to fill; `w0[0..16)` byte length (16 bytes per vertex); `w1` segment address of the array |
| `0x06` | `G_DL` | `w0[16..24)` flag: `0` call, `1` jump (never returns); `w1` segment address of the list |
| `0xb1` | `G_TRI4` | four triangles per command; not decoded by the walker |
| `0xb8` | `G_ENDDL` | ends the list |
| `0xfd` | `G_SETTIMG` | `w0[21..24)` format, `w0[19..21)` pixel size (0=4b, 1=8b, 2=16b, 3=32b); `w1` segment address |
| `0xc0` | `G_SETTEX` | Rare extension; no effect |

All other opcodes are RDP or state commands with no addresses. A vertex
is the standard 16-byte F3D layout.

## Where vertex arrays are declared

Model display-list nodes (`ModelRoData_DisplayListRecord` and siblings)
carry an explicit vertex pointer and count next to their display-list
pointers, so model vertex arrays do not have to be inferred from
`G_VTX`. bg rooms load a separate compressed vertex blob per room;
their lists reference it only through `G_VTX`.
