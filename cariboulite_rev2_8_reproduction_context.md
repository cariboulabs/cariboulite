# CaribouLite Rev 2.8 Reproduction Project — Codex Handoff Context

**Prepared:** 2026-09-12  
**Primary working environment:** VS Code + Codex  
**Target manufacturer:** Eurocircuits  
**Initial build quantity:** 5 × CaribouLite Full Rev 2.8  
**Project phase:** Manufacturing feasibility / preparation of upload package

## Current status note — 2026-10-16

This is the September 12 handoff, retained as historical project context. The
quotation package has since been created under
[manufacturing/rev2.8-full-eurocircuits](manufacturing/rev2.8-full-eurocircuits/README.md).
Its [technical review](manufacturing/rev2.8-full-eurocircuits/technical-review.md)
supersedes the preliminary feasibility and stack-up summary below: the source
contains **1,397 blind vias with 0.1016 mm holes**, and its **1.610868 mm**
stack total includes solder mask (copper/dielectric subtotal **1.560068 mm**).
Fabrication compatibility still needs manufacturer confirmation. U25, J7 and
SH1/SH2 decisions are recorded in the package; shield cans are not fitted.

The sourcing and budget sections are dated estimates, not current availability
or a quotation. The package remains prepared for technical review/quotation,
with no manufacturing release recorded in this repository. The next steps are
manufacturer preflight, unresolved procurement checks and quote review, rather
than recreating the existing package. See the
[procurement policy](manufacturing/rev2.8-full-eurocircuits/procurement-policy.md).

---

## 1. Project objective

The immediate objective is to reproduce the **original CaribouLite Full Rev 2.8 hardware exactly as released**, using the public CaribouLabs production data.

The first production run is intended to contain **five assembled Full Rev 2.8 boards**.

This is deliberately a reproduction project, **not a redesign**.

The purpose of the first run is to determine whether newly manufactured Rev 2.8 boards can be made to behave indistinguishably from an original known-good Rev 2.8 CaribouLite.

If this succeeds, a later phase may consider a larger run and potentially reviving/maintaining the CaribouLite project. Those future possibilities must not influence the first reproduction.

---

## 2. Non-negotiable engineering rule

### DO NOT MODIFY THE ORIGINAL DESIGN FOR THE FIRST BUILD.

In particular:

- no schematic changes;
- no PCB layout changes;
- no RF matching changes;
- no FPGA substitution;
- no RF215 substitution;
- no mixer substitution;
- no oscillator/TCXO substitution;
- no footprint changes;
- no "modernization";
- no component-value optimization;
- no routing cleanup;
- no changes intended merely to make manufacture easier.

Where an original manufacturer ordering code has been superseded by a new ordering code, it may only be accepted after confirming that the manufacturer considers it the same component/product and that the electrical, RF, mechanical and footprint characteristics are unchanged.

Any unavoidable deviation must be documented and explicitly approved before it enters the manufacturing package.

---

## 3. Repository

The CaribouLite repository is already cloned locally.

Typical local location used by Jan:

```text
~/src/cariboulite
```

The public upstream hardware repository is:

```text
https://github.com/cariboulabs/cariboulite_hw
```

Jan's software/development repository is:

```text
https://github.com/Habraken/cariboulite
```

The production artifacts of interest are under:

```text
hardware/rev2/
```

Do not assume the Git working tree is disposable. Inspect before changing anything and prefer creating a dedicated reproduction/manufacturing working directory.

---

## 4. Confirmed Rev 2.8 production files

The repository contains an unusually complete Rev 2.8 manufacturing package.

### Assembly

```text
hardware/rev2/assembly/cariboulite_ad.PDF

hardware/rev2/assembly/full_bom/cariboulite_bom.xlsx
hardware/rev2/assembly/full_pnp/cariboulite_full_pnp.txt

hardware/rev2/assembly/ism_bom/cariboulite_ism_bom.xlsx
hardware/rev2/assembly/ism_pnp/cariboulite_ism_pnp.txt

hardware/rev2/assembly/dxf/bottom_emi_drawing.dxf
hardware/rev2/assembly/dxf/top_emi_drawing.dxf
```

For the initial project, the **Full** BOM and PnP files are the relevant variant.

### PCB Gerbers

```text
hardware/rev2/pcb/gerber/cariboulite_r2.8.apr
hardware/rev2/pcb/gerber/cariboulite_r2.8.APR_LIB
hardware/rev2/pcb/gerber/cariboulite_r2.8.EXTREP
hardware/rev2/pcb/gerber/cariboulite_r2.8.G1
hardware/rev2/pcb/gerber/cariboulite_r2.8.G2
hardware/rev2/pcb/gerber/cariboulite_r2.8.GBL
hardware/rev2/pcb/gerber/cariboulite_r2.8.GBO
hardware/rev2/pcb/gerber/cariboulite_r2.8.GBP
hardware/rev2/pcb/gerber/cariboulite_r2.8.GBS
hardware/rev2/pcb/gerber/cariboulite_r2.8.GD1
hardware/rev2/pcb/gerber/cariboulite_r2.8.GD2
hardware/rev2/pcb/gerber/cariboulite_r2.8.GD3
hardware/rev2/pcb/gerber/cariboulite_r2.8.GTL
hardware/rev2/pcb/gerber/cariboulite_r2.8.GTO
hardware/rev2/pcb/gerber/cariboulite_r2.8.GTP
hardware/rev2/pcb/gerber/cariboulite_r2.8.GTS
hardware/rev2/pcb/gerber/cariboulite_r2.8.Outline
hardware/rev2/pcb/gerber/cariboulite_r2.8.REP
hardware/rev2/pcb/gerber/cariboulite_r2.8.RUL
```

### Drill

```text
hardware/rev2/pcb/ncdrill/cariboulite_r2.8.DRR
hardware/rev2/pcb/ncdrill/cariboulite_r2.8.LDP
hardware/rev2/pcb/ncdrill/cariboulite_r2.8.TX1
hardware/rev2/pcb/ncdrill/cariboulite_r2.8.TX2
hardware/rev2/pcb/ncdrill/cariboulite_r2.8.TXT
```

### Stack-up

```text
hardware/rev2/pcb/stack/cariboulite_r2.8.xls
```

### Schematics

```text
hardware/rev2/schematics/CaribouLite.PDF
hardware/rev2/schematics/CaribouLite_ism.PDF
```

The Full schematic is the relevant one for this build.

---

## 5. Original Rev 2.8 PCB stack-up

The production stack report has been inspected.

The board is a nominal **1.6 mm, four-layer FR-4 PCB**.

Approximate reported construction:

| Layer / material | Thickness |
|---|---:|
| Top copper | 1.38 mil ≈ 35 µm |
| FR-4 prepreg, εr ≈ 4.3 | 4.33 mil ≈ 110 µm |
| Inner copper 1 | 1.38 mil ≈ 35 µm |
| FR-4 core, εr ≈ 4.3 | 47.24 mil ≈ 1.20 mm |
| Inner copper 2 | 1.38 mil ≈ 35 µm |
| FR-4 prepreg, εr ≈ 4.3 | 4.33 mil ≈ 110 µm |
| Bottom copper | 1.38 mil ≈ 35 µm |

Reported total:

```text
63.42 mil ≈ 1.61 mm
```

Correction from the later package review: this total includes two 1 mil mask
entries. Copper plus dielectric is 61.42 mil = 1.560068 mm; see the
[extracted stack table](manufacturing/rev2.8-full-eurocircuits/technical-review.md#stack-up-important-correction-to-the-handover).

### Important RF consideration

The approximately **110 µm distance between the outer copper and adjacent reference plane** is important.

The board operates into the GHz range, including the Full version's high-frequency RF path. Do not treat this simply as an arbitrary generic 1.6-mm four-layer FR-4 board.

When preparing the Eurocircuits quote:

1. compare their available four-layer stack-ups against the original;
2. preserve outer-layer-to-plane geometry as closely as possible;
3. determine the impedance consequence of any difference;
4. do not alter RF trace widths to compensate unless a redesign is explicitly authorized.

For the first reproduction, preference is to reproduce the original physical construction rather than adapt the design to a substantially different stack.

---

## 6. BOM

The Full Rev 2.8 production BOM is:

```text
hardware/rev2/assembly/full_bom/cariboulite_bom.xlsx
```

It contains approximately 50 BOM lines and provides manufacturer part numbers.

A separate sourcing analysis has already been performed.

The sourcing snapshots are present at the repository root:
[first pass](cariboulite_rev2_8_bom_sourcing_first_pass.md) and
[manufacturer follow-up](cariboulite_rev2_8_bom_sourcing_manufacturer_update.md).

---

## 7. Important BOM devices

Key devices include:

```text
AT86RF215-ZU
RFFC5072TR13
ICE40LP1K-QN84
ATX-12-F-26.000MHz-F05-T
SG-8018CG 125.0000M-TJHSA3
MAAM-011229
SKY13373-460LF
CG2164X3-C2
CG2409X3-C2
LFCN-2250+
HFCN-2275+
TC4-19G2+
TC1-1-13M+
2450FB15A050E
0896BM15E0025E
```

The BOM also contains RF matching capacitors/inductors with very small values, including parts around:

```text
0.8 pF
1.8 pF
2.2 pF
5.6 nH
```

These are RF components and must not be treated like generic interchangeable passives.

---

## 8. Sourcing conclusions recorded 2026-09-12

A first distributor and manufacturer-level sourcing pass was performed on 2026-09-12.

### Overall conclusion

A five-board Rev 2.8 Full reproduction currently appears:

> **Preliminary sourcing assessment: potentially feasible.**

The subsequent blind-via/stack-up review identified unresolved fabrication
holds; this sourcing assessment does not establish manufacturing feasibility.

No critical RF silicon checked so far appears to be an unavoidable obsolete showstopper.

Stock levels are time-sensitive and must be rechecked immediately before ordering.

### AT86RF215-ZU

Microchip lists the AT86RF215 as in production.

The exact `AT86RF215-ZU` has been available through normal authorized distribution in prototype quantities.

Assessment:

```text
LOW RISK
EXACT MPN REQUIRED
```

### RFFC5072TR13

Qorvo lists the RFFC5072 as a current Production/Standard product.

It can be obtained through distribution and Qorvo's own purchasing channels in small quantities.

Assessment:

```text
LOW RISK
EXACT MPN REQUIRED
```

### ICE40LP1K-QN84

The exact Lattice FPGA has been found in distribution stock.

Factory lead time beyond available stock may be long.

Assessment:

```text
MEDIUM SUPPLY RISK
BUY PROTOTYPE QUANTITY EARLY
EXACT MPN REQUIRED
```

### MAAM-011229

MACOM continues to support the device and offers ordering/sample/distributor routes.

Assessment:

```text
LOW RISK
EXACT MPN REQUIRED
```

### SKY13373-460LF

Healthy distributor availability was found.

Assessment:

```text
LOW RISK
EXACT MPN REQUIRED
```

### CG2164X3-C2 / CG2409X3-C2

The CEL RF devices remain obtainable/listed.

Recheck exact suffix/packaging immediately before ordering.

Assessment:

```text
LOW TO MEDIUM RISK
EXACT MPN REQUIRED
```

### Mini-Circuits LFCN-2250+, HFCN-2275+, TC4-19G2+

These remain current/listed products and have been found through authorized channels and/or Mini-Circuits direct.

Assessment:

```text
LOW RISK
EXACT MPN REQUIRED
```

### TC1-1-13M+

Mini-Circuits still maintains the exact product page, datasheet, S-parameters and sample-request facility.

At the time of checking, direct/current distributor stock was limited or zero.

This appears to be a **stock/lead-time problem, not an obsolescence problem**.

Action:

- check Mini-Circuits direct;
- ask about 5–10 prototype pieces;
- consider sample request if appropriate;
- do not substitute automatically.

Assessment:

```text
MEDIUM/HIGH PROCUREMENT ATTENTION
NOT CURRENTLY A DESIGN BLOCKER
```

### ATX-12-F-26.000MHz-F05-T

This is the exact Abracon 26 MHz TCXO used by production Rev 2.8.

Abracon lists it as **Active**.

Important characteristics include approximately:

```text
26 MHz
±0.5 ppm
clipped sine
2.5 × 2.0 mm class package
```

Small-quantity procurement appears possible, although normal distributor stock can be intermittent.

This component deserves special treatment because clock integrity has already been implicated in real CaribouLite hardware failures.

Assessment:

```text
HIGH PROCUREMENT PRIORITY
EXACT MPN ONLY
TRACEABLE SOURCE REQUIRED
NO SUBSTITUTE
```

### Coilcraft 0402CS-5N6XGLW

An initial distributor check suggested the exact catalog entry was obsolete.

Manufacturer-level checking improved this result considerably.

Coilcraft still offers the **5.6 nH 0402CS family**, including direct purchasing/sample facilities.

The outstanding issue is decoding the historic:

```text
0402CS-5N6XGLW
```

suffix against Coilcraft's current ordering nomenclature for tolerance, termination and packaging.

Do not call this component obsolete until that suffix analysis has been completed.

Assessment:

```text
MEDIUM ATTENTION
LIKELY SOLVABLE
RF MATCHING COMPONENT — NO UNVERIFIED SUBSTITUTE
```

### Johanson 2450FB15A050E

Johanson identifies the old part-number family as a legacy ordering number corresponding to the current global MPN:

```text
2450FB15A0050001E
```

The current product is in production and is specifically associated with AT86RF215-family matching.

Before accepting the current ordering code, compare:

- package;
- land pattern;
- insertion loss;
- return loss;
- impedance;
- amplitude balance;
- phase balance;
- frequency response.

If manufacturer documentation confirms identity, document this as an **MPN migration**, not a circuit substitution.

### Johanson 0896BM15E0025E

Likewise, the legacy number maps to current global MPN:

```text
0896BM15E0025001E
```

The same verification rule applies.

---

## 9. Private-individual procurement

No critical component investigated so far appears to require the purchaser to be a large corporation.

Practical purchasing routes include:

- Mouser;
- DigiKey;
- manufacturer-direct web stores;
- manufacturer sales;
- sample requests;
- Eurocircuits component sourcing.

Manufacturers checked include:

- Microchip;
- Qorvo;
- MACOM;
- Mini-Circuits;
- Abracon;
- Coilcraft;
- Johanson Technology.

For difficult parts, customer-supplied components to Eurocircuits are an acceptable option.

---

## 10. Procurement strategy for the first five boards

Use three sourcing paths.

### A. Eurocircuits sourced

Prefer Eurocircuits sourcing for:

- ordinary resistors;
- ordinary capacitors;
- generic decoupling;
- LEDs;
- straightforward connectors;
- regulators and other devices where the exact BOM MPN is readily available.

However, the exact BOM MPN should still be specified for the first build.

### B. Authorized distributor sourced

Use Mouser/DigiKey or equivalent authorized distribution for readily available critical devices.

### C. Customer supplied / manufacturer direct

Consider buying and supplying difficult exact components ourselves, especially:

```text
ATX-12-F-26.000MHz-F05-T
TC1-1-13M+
0402CS-5N6XGLW
```

if Eurocircuits cannot source them reliably.

---

## 11. Explicit no-substitution list

For the first build, the manufacturer should be told:

> **DO NOT SUBSTITUTE WITHOUT WRITTEN APPROVAL**

for at least:

- RF matching capacitors;
- RF matching inductors;
- RF baluns;
- RF filters;
- RF transformers;
- AT86RF215;
- RFFC5072;
- RF amplifiers;
- RF switches;
- FPGA;
- 125 MHz oscillator;
- 26 MHz TCXO.

In particular, do not allow automatic BOM-tool substitution based solely on nominal capacitance, inductance, frequency, voltage or package.

---

## 12. Known-good hardware reference

Jan has one surviving **known-good CaribouLite Full Rev 2.8**.

This is extremely valuable.

The reproduced boards can be A/B tested against it using identical:

- Raspberry Pi;
- OS image;
- kernel;
- CaribouLite driver/software;
- FPGA bitstream;
- cables;
- antennas;
- RF test setup.

The reference board should not be modified during this reproduction project.

---

## 13. Relevant previous hardware failure

A second original CaribouLite board developed/presented persistent problems including:

- RX SMI synchronization failure;
- unstable/dirty TX;
- behavior strongly suggesting a hardware fault rather than purely software;
- clock/PLL behavior different from the good board.

The 26 MHz TCXO was investigated.

The bad board used:

```text
ATX-12-F-26.000MHz-F05-T
```

The TCXO itself measured approximately 26 MHz, but clock/PLL behavior remained suspicious.

Observed PLLCF examples:

```text
good board: 0x1C
bad board:  0x1D / 0x1E
```

An attempt to replace the TCXO ultimately damaged/destroyed the bad board.

This experience is one reason the exact clock implementation must be reproduced rather than redesigned during the first manufacturing attempt.

---

## 14. Existing software / FPGA validation capability

Jan has substantial CaribouLite software and FPGA development experience.

Known working pipeline includes approximately:

```text
Audio 48 kS/s
→ NBFM
→ 4 MSPS IQ16
→ software FIFO
→ SMI
→ FPGA
→ LVDS
→ AT86RF215
```

The system has working TX/RX code, low-level register access and FPGA development capability.

Typical test frequencies include around:

```text
430.1 MHz
```

Both S1G and HiF channels are used.

This means newly manufactured boards can receive much deeper validation than merely "does Linux detect the HAT?"

---

## 15. First-article validation concept

Do not immediately treat all five boards as accepted merely because assembly succeeds.

A suggested first-article validation sequence is:

1. Visual inspection.
2. Check for shorts on primary rails before power.
3. Power from a controlled/test setup if practical.
4. Verify supply rails.
5. Verify 26 MHz TCXO.
6. Verify 125 MHz oscillator.
7. Verify SPI communication.
8. Verify FPGA identification/programming.
9. Verify AT86RF215 communication.
10. Run CaribouLite self-test.
11. Verify SMI synchronization.
12. Verify RX IQ stream on S1G.
13. Verify RX IQ stream on HiF.
14. Verify CW TX.
15. Compare RF frequency error with original board.
16. Compare TX spectrum with original board.
17. Compare RX behavior/sensitivity qualitatively, then quantitatively if test equipment permits.
18. Run sustained RX/TX tests.

Record results per board serial/identifier.

---

## 16. Initial budget estimate

Before a formal Eurocircuits quote, the estimated five-board budget is approximately:

| Category | Estimated total |
|---|---:|
| Components | €530–800 |
| PCB + assembly/setup/inspection | €600–1,200 |
| **Estimated five-board total ex VAT** | **€1,130–2,000** |

A useful planning figure is:

```text
≈ €1,500 ex VAT total
≈ €300 per assembled board ex VAT
```

At 21% VAT:

```text
≈ €1,815 total
≈ €363 per board
```

These are engineering estimates only.

The Eurocircuits quote should replace them as soon as the upload package is validated.

---

## 17. Codex task: prepare Eurocircuits upload package

The next major project task is to create a **clean, auditable Eurocircuits quotation/upload package** without altering the design.

Suggested working directory:

```text
manufacturing/rev2.8-full-eurocircuits/
```

Suggested structure:

```text
manufacturing/rev2.8-full-eurocircuits/
├── README.md
├── source_manifest.md
├── CHANGELOG.md
├── pcb/
│   ├── gerber/
│   ├── drill/
│   └── stackup/
├── assembly/
│   ├── bom/
│   ├── pnp/
│   └── drawings/
├── mechanical/
│   └── emi_shields/
├── reference/
│   └── schematics/
└── procurement/
    ├── sourcing_status.md
    ├── no_substitution.md
    └── customer_supplied_parts.md
```

Do not copy unnecessary generated/cache files into the package.

---

## 18. Upload-package work plan

Codex should proceed in small, reviewable steps.

### Phase 1 — inventory and integrity

- inventory every source production file;
- calculate SHA-256 hashes;
- record original repository-relative paths;
- identify file format/version where possible;
- do not modify source files.

Generate:

```text
source_manifest.md
```

### Phase 2 — Gerber/drill validation

Determine:

- copper-layer count;
- layer mapping;
- solder mask;
- paste;
- silkscreen;
- board outline;
- plated/non-plated drill interpretation;
- units;
- coordinate format;
- whether auxiliary files are required by Eurocircuits.

Do not rename/remove files until their function is understood.

### Phase 3 — stack-up

Extract the original Rev 2.8 stack-up into a human-readable document.

Compare it with available Eurocircuits four-layer builds.

Document:

- copper thickness;
- dielectric thickness;
- total thickness;
- dielectric material/Er;
- closest Eurocircuits stack;
- differences;
- likely RF implications.

Do not compensate by modifying layout.

### Phase 4 — BOM normalization

Create a manufacturer-friendly derivative BOM while preserving the original BOM unchanged.

For each line capture:

- designator(s);
- quantity per board;
- total quantity for five;
- value;
- manufacturer;
- exact MPN;
- description;
- package/footprint if available;
- sourcing status;
- manufacturer lifecycle;
- distributor;
- customer-supplied flag;
- substitution allowed: YES/NO;
- notes.

Any generated BOM must retain traceability to the original row.

### Phase 5 — PnP validation

Inspect:

```text
cariboulite_full_pnp.txt
```

Determine:

- units;
- origin;
- rotation convention;
- side naming;
- designator consistency against BOM;
- whether all fitted components appear;
- whether DNP parts exist;
- whether Eurocircuits needs transformation.

If transformation is required, create a derivative file and document the transformation. Never overwrite the original.

### Phase 6 — assembly documentation

Inspect:

```text
cariboulite_ad.PDF
```

Extract all relevant:

- assembly notes;
- polarity/orientation requirements;
- special handling;
- shield information;
- variant notes;
- manufacturing instructions.

### Phase 7 — EMI shielding — resolved

Inspect:

```text
top_emi_drawing.dxf
bottom_emi_drawing.dxf
```

SH1 and SH2 are explicitly approved as not fitted for this reproduction. The
physical reference board has no shield cans installed, consistent with the
crossed-out schematic references and their absence from the BOM/PnP. Retain the
shield drawings as source evidence only; do not source, quote, manufacture or
install shield cans.

### Phase 8 — Eurocircuits preflight

Prepare the package so it can be uploaded to Eurocircuits for quotation.

At this stage the goal is:

> **Obtain a quotation, not place an order.**

Capture screenshots/reports/errors from the Eurocircuits Visualizer/BOM processing as appropriate and record every manufacturing warning.

---

## 19. Auditability rule

Every derivative manufacturing file must be reproducible from the original source.

If a script transforms a file:

- commit the script;
- document its purpose;
- document input/output;
- preserve the original;
- preferably include hashes.

Avoid hand-editing manufacturing files where a deterministic transformation can be scripted.

---

## 20. Git workflow recommendation

Before starting:

```bash
cd ~/src/cariboulite
git status
git branch --show-current
git log -1 --oneline
```

Do not destroy or reset uncommitted work.

A dedicated branch is recommended, for example:

```bash
git switch -c rev2.8-reproduction
```

Only do this after checking the current working tree.

Keep manufacturing-package preparation separate from functional software changes.

---

## 21. Things Codex must not infer

If information is missing, stop and inspect the source or ask Jan.

Do not infer:

- DNP status from absence alone;
- component substitutions;
- PCB layer meaning from filename alone where ambiguous;
- PnP rotation convention;
- shield-can dimensions not contained in source data;
- impedance requirements without checking geometry/source documentation;
- manufacturer equivalence from similar part numbers;
- whether Eurocircuits warnings are harmless.

This is RF hardware. Small manufacturing differences can be electrically significant.

---

## 22. Immediate next actions

Recommended order:

1. Create the dedicated reproduction working directory.
2. Copy this context document into it.
3. Add the latest BOM sourcing report.
4. Inventory and hash the original production files.
5. Inspect `cariboulite_ad.PDF`.
6. Parse and document the Full PnP file.
7. Create a clean layer/drill map.
8. Compare the original stack-up with Eurocircuits.
9. Continue closing unresolved BOM sourcing items.
10. Build the quotation upload package.
11. Run Eurocircuits preflight/Visualizer.
12. Review all warnings manually.
13. Obtain quote for 5 boards.
14. Do **not** order until the package and quote have been reviewed.

---

## 23. Current decision gate

The project should proceed to manufacturing only if all of the following are true:

- PCB stack-up is acceptably reproducible;
- Gerber/drill package passes manufacturer preflight;
- PnP interpretation is unambiguous;
- BOM/DNP interpretation is unambiguous;
- all critical exact components are obtainable;
- no unauthorized substitutions are proposed;
- the SH1/SH2 not-fitted decision remains documented in the production package;
- assembly documentation is understood;
- total quote remains acceptable;
- first-article test plan is ready.

---

## 24. Project philosophy

The first milestone is not:

> "make a better CaribouLite."

It is:

> **"manufacture a new Rev 2.8 CaribouLite that behaves like the surviving original."**

Once that has been demonstrated, redesigns, component modernization, improved manufacturing files, updated Raspberry Pi support, revised FPGA architecture or a future hardware revision can be considered as separate projects.

For now, **preserve the reference design**.
