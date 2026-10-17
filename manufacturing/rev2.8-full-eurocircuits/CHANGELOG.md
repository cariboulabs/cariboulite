# Package change log

## 2026-10-16 — Documentation archive synchronization

- Refreshed `SHA256SUMS` and the support ZIP to include the existing September 13
  README, technical-review and change-log corrections. The previous checksum
  manifest/archive still contained the earlier versions of those three documents.
- Verified all 34 source-manifest entries and all 31 copied originals against the
  repository sources. PCB upload ZIP, assembly BOM/CPL, procurement data and
  validation outputs are byte-for-byte unchanged by this documentation update.
- No new stock check, manufacturer preflight, upload, order or manufacturing
  release was performed.

## 2026-09-13 — macOS reproducibility hardening

- Excluded Finder `.DS_Store` metadata from source inventory and package archives.
- Removed stale copied Finder metadata during regeneration so package inputs are
  independent of whether the builder runs on macOS or Linux.
- Resolved U25 to exact MPN `SG-8018CG 125.0000M-TJHSA3`: the schematic and
  primary BOM MPN agree on the standby (`S`) variant; `TJHPA3` in the original
  description/comment/PnP is retained as stale-source evidence, not an approved
  substitution.
- Resolved J7 as fitted with exact MPN `CONUFL001-SMD-T`, supported by the BOM,
  PnP, assembly drawing and physical reference board. Clarified that J7 lies
  outside the schematic DO NOT PLACE boundary while R25, C108 and C122 remain
  DNP and leave the connector electrically isolated.
- Resolved SH1 and SH2 as not fitted, consistent with the schematic, BOM/PnP
  and Jan's physical reference board. Removed shield supply/fitting from
  the reproduction and quotation scope; retained drawings as source evidence.

## 2026-09-12 — Initial technical quotation package

- Copied Full/shared production originals without changing file contents.
- Inventoried source paths, sizes and SHA-256 hashes; excluded ISM variant.
- Created separate PCB ZIP, BOM CSV and CPL CSV for quotation upload.
- Selected first explicitly listed original J1 MPN for quotation.
- Kept all other primary MPNs; flagged U25 and J7 source ambiguities.
- Excluded four schematic-confirmed DNP placements and bare test point TP13.
- Aligned placement coordinates to Gerbers by verified common translation,
  converted mil to mm, normalized 360° to 0°, and corrected J1's centre to the
  centroid of its 40 drilled holes. Preserved source sides and other angles.
- Identified 1,397 blind-via holes omitted from the handover's feasibility summary.
- Corrected stack thickness interpretation: reported total includes mask.
- Documented shielding, manufacturing and assembly release holds.
- Added deterministic generation and validation outputs. No PCB design change,
  supplier contact, upload, procurement or manufacturing release performed.
