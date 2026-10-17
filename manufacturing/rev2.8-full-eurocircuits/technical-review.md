# Technical review and quotation holds

Source paths below are relative to `originals/`. This review distinguishes
source evidence from proposed quotation assumptions. Original geometry and
MPNs remain available unchanged; no PCB edits have been made.

## PCB layers and drill programs

The original `.EXTREP` maps GTL/G1/G2/GBL to Component Side/Layer 1/Layer 2/Solder
Side. Copper headers independently identify physical order 1/2/3/4.
Gerber headers specify RS-274X, absolute inch coordinates, 2 integer and 5 decimal
digits, leading-zero omission. Excellon headers specify inch, LZ, 2:5.

| Original file | Role / physical copper span | Actual coordinate hits |
|---|---|---:|
| `cariboulite_r2.8.TXT` | Through 1–4: tools T2–T6 plated, T7 non-plated | 856 |
| `cariboulite_r2.8.TX1` | Blind 1–2, T1 plated, 0.1016 mm | 661 |
| `cariboulite_r2.8.TX2` | Blind 3–4, T1 plated, 0.1016 mm | 736 |

The through file contains 805 × 0.2032 mm, 4 × 0.459994 mm, 2 × 0.599948 mm,
2 × 0.8001 mm, 40 × 0.899922 mm plated holes and 3 × 2.750058 mm non-plated
holes. Counts were parsed from the drill programs and match `.DRR`: 2,253 total.
The plating comments apply to tool declarations; the trailing NON_PLATED comment
in the blind-file header does not make the already-declared T1 non-plated.

The `.DRR` explicitly names the layer pairs. `.LDP` corroborates them, but its
blind-via DrillFile fields are blank. Reports use the older `cariboulite_r2`
basename; actual delivered filenames use `cariboulite_r2.8`. Do not treat this
as missing drill data. Preserve all three drill programs and configure their
spans explicitly in the manufacturer's tools.

`.GD1/.GD2/.GD3` are **drill drawings**, as identified by `.EXTREP`, not drilling
programs. They, `.apr/.APR_LIB`, `.REP`, `.EXTREP`, `.RUL` and `.LDP` remain in
the support package but are excluded from the PCB upload archive. Extended
Gerbers already contain their aperture definitions.

The outline contains repeated contour commands. Its coordinate extrema imply
approximately 64.99987 × 30.029912 mm; assembly page 3 labels the board 65 × 30 mm.
No rounding or deduplication was applied. The `.REP` also says the outline was
included in copper exports; CAM must review edge copper/outline interpretation.
These local checks are not a full Gerber connectivity/clearance or DFM analysis.

## Stack-up: important correction to the handover

Extracted directly from `pcb/stack/cariboulite_r2.8.xls`, sheet
`Board Stack Report`, rather than copied from the handover summary:

| Layer | Source material | Thickness mil | Thickness mm | Source dielectric constant |
|---|---|---:|---:|---:|
| Top solder mask | White | 1.00 | 0.025400 | 4.0 |
| Copper 1 | Copper | 1.38 | 0.035052 | — |
| Dielectric 1 | PrePreg | 4.33 | 0.109982 | 4.3 |
| Copper 2 | Copper | 1.38 | 0.035052 | — |
| Centre dielectric | FR-4 | 47.24 | 1.199896 | 4.3 |
| Copper 3 | Copper | 1.38 | 0.035052 | — |
| Dielectric 2 | PrePreg | 4.33 | 0.109982 | 4.3 |
| Copper 4 | Copper | 1.38 | 0.035052 | — |
| Bottom solder mask | White | 1.00 | 0.025400 | 4.0 |

The copper/dielectric subtotal is **61.42 mil = 1.560068 mm**. The report's
**63.42 mil = 1.610868 mm** includes its two 1 mil mask entries. The previous
handover attributed that total to copper/dielectrics alone. Ask the manufacturer
to clarify finished thickness, copper treatment and tolerances against this
actual report. The report does not establish the as-manufactured mask colour;
white is the recorded material name, not a newly chosen cosmetic specification.

The export's blind spans are geometrically clear, but the drilling method is not
specified by these files. Do not assume that the 0.1016 mm holes can be produced
by the manufacturer's standard mechanical process or replace them with through
vias. Eurocircuits describes blind-via manufacture using a reversed build with
outer cores, and states it does not use laser drilling for this purpose.
That differs from the source report's outer prepregs/centre core construction.
A matching build has **not** been established. Their CAM team must confirm
whether the same layer geometry and connectivity are reproducible without a
design change. [Eurocircuits blind-via process](https://www.eurocircuits.com/tips-tricks/blind-and-buried-vias/)
(checked 2026-09-12).

No RF impedance calculation or trace-width compensation has been applied.
Surface finish, laminate grade, mask colour, finished copper specification,
via fill/cap requirements and production tolerances are not sufficiently
established by this package to select silent defaults.

## BOM and population

The original Full spreadsheet has 50 lines and 205 unique designators. Every
quantity equals its designator count. All 205 occur in the 210-row PnP export.
The derivative BOM preserves the exact primary MPN, description and footprint
strings except the explicit J1 quotation selection below. Footprint names such
as MURATA/PANASONIC are CAD library names, not manufacturer identifications.
Manufacturer/supplier fields in the procurement audit remain unverified.

Five source placements are excluded from the 205-row assembly CPL based on
independent schematic evidence:

| Designator | Evidence / treatment |
|---|---|
| R10, R12 | Full schematic page 2: red cross and DNP annotation; not fitted |
| L19, U29 | Full schematic page 6: crossed out in DO NOT PLACE oscillator block; not fitted |
| TP13 | Page 3: 1V2 test point, source footprint TP_40; bare PCB feature, no purchased part |

These are the five **extra PnP rows**, not an exhaustive list of all DNP features
in the schematic. Do not auto-populate other pads absent from the Full BOM.

## BOM decisions

- **U25 - resolved 2026-09-13:** use exact MPN
  `SG-8018CG 125.0000M-TJHSA3`. The schematic and primary BOM MPN both specify
  the standby (`S`) variant. `TJHPA3` in the original description, comment and
  PnP is treated as stale shared metadata. Epson defines `S` as standby and `P`
  as output enable; U25 pin 1 is tied to `VCC_FP_DIG_3V3`, so either operates
  continuously in this circuit, but `P` is not an approved substitution for
  this exact reproduction. Evidence: Full schematic page 4 and Epson SG-8018
  datasheet, Product Number / Ordering Code section:
  <https://download.epsondevice.com/td/pdf/app/SG-8018CA_en.pdf>.
- **J7 - resolved 2026-09-13:** populate J7 with exact MPN
  `CONUFL001-SMD-T`. The BOM, PnP and assembly drawing include it, and Jan
  confirmed it is fitted on the physical reference board. Enlarged inspection
  of Full schematic page 6 shows J7 outside the dashed DO NOT PLACE boundary;
  that boundary encloses R25, C108 and C122. Leave those three parts DNP so J7
  remains electrically isolated in the released configuration. Its source
  comment `#NAME?` is preserved in the audit, not used as an MPN.
- **J1:** the original MPN cell explicitly lists three alternatives:
  `M20-7832046`, `PPPC202LFBN-RC`, `61304021821`. The upload BOM selects the
  first, `M20-7832046`, which also matches the original comment. This is an
  original listed option, not an engineered substitute. Other two remain in
  Original MPN in the audit. Confirm socket height/orientation with the reference.
- **U21:** description mentions `TC4-19+`, but primary MPN is `TC4-19G2+`.
  Only the primary is requested; the description is not substitution permission.

## Placement transformation

The source PnP uses mils and an origin different from the Gerbers. All 210
source Pad X/Y positions match a flash on their stated side's copper layer
after applying:

```text
Gerber X [mil] = source X [mil] + 12697.26
Gerber Y [mil] = source Y [mil] +  7261.81
millimetres = mil × 0.0254
```

Matching tolerance is 0.1 mil per axis; actual residuals are recorded for every
reference in `validation/pad-alignment.csv`. This confirms a common translation
and side-coordinate orientation; it does **not** validate every component's
body-centre or pin-1 rotation convention.

For 204 fitted components, CPL uses translated source **Mid** X/Y. J1 is the
documented exception: source Mid Y = 298.756 mil lies away from the header.
The centre of the forty T6 drilled holes is (11625.76, 7132.81) mil in Gerber
coordinates, agreeing with translated source Ref X/Y = (-1071.5, -129) mil.
That hole-pattern centre is used for J1's CPL centre. This is a placement-data
correction only; no pads, holes or component choice changed.

Top/Bottom are mapped directly from source T/B. Source angles are retained,
with 360° normalized to 0°; no bottom-side mirroring or rotation compensation
is applied. In particular, J1 remains source side Top, rotation 180°: confirm
the required insertion/mating side and the manufacturer's model convention.
Check every polarized part and pin-1 orientation in Assembly Visualizer before
release. The large absolute mm coordinates intentionally match the original
Gerber origin; do not independently reset one file's origin during import.

## Drawings and shielding

`assembly/cariboulite_ad.PDF` is four pages of vector drawing graphics with no
usable extracted text. All pages were visually inspected: pages 1/2 show the
opposite populated sides; pages 3/4 show board/shield-region dimensions. Title
block is R2.8, drawing DN00002, dated 06/06/2022. The file does not provide a
complete textual assembly/process specification.

DXFs declare millimetres. `top_emi_drawing.dxf` contains 12 LINE and 2 CIRCLE
entities; `bottom_emi_drawing.dxf` contains 11 LINE and 2 CIRCLE entities.
Their geometric extents, including circles, are approximately 35.554 × 15.610
mm and 58.992 × 22.595 mm respectively. These are **drawing extents**, not
approved finished can dimensions. Both are planar drawings; no purchasable
shield MPN, wall height, metal thickness or material/plating specification was
identified in them. The assembly PDF's dimensions must remain authoritative
alongside the original DXFs for technical discussion.

**SH1/SH2 - resolved 2026-09-13:** do not fit shield cans. SH1 and SH2 are
crossed out on Full schematic page 2, absent from the Full BOM/PnP, and Jan
confirmed that neither is installed on the physical reference board. Shield
supply and fitting are therefore excluded from the reproduction and quotation
scope. Retain the original DXFs and assembly drawing only as source evidence;
they do not authorize shield manufacture or installation.

## Local validation versus manufacturing approval

Local checks cover source integrity, column parsing, quantity/designator
reconciliation, actual drill counts, copper order, coordinate translation and
archive integrity. They do not establish current component stock, electrical
equivalence of revised MPNs, assembly rotations, Gerber DRC, or fabrication
feasibility. Manufacturer preflight and its warning review remain outstanding.
No files have been uploaded and no order or message has been sent.
