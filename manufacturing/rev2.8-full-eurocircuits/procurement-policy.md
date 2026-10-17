# Procurement instructions for quotation

Use exact original BOM parts. Do not substitute RF ICs, FPGA, switches,
amplifiers, filters, baluns, transformers, RF passives or either oscillator.
The same exact-MPN default applies to ordinary passives and connectors.

The supplied BOM's Quantity is per board. Five-board consumption is shown only
in `procurement/bom-audit.csv`; assembly spares, attrition allowances and reel
minimums are additional quantities to be agreed with the assembler.

The earlier sourcing handover is a historical research snapshot, not verified
stock or a procurement release. No new distributor availability check was
performed for this package. The audit intentionally marks sourcing and lifecycle
as unverified rather than presenting those historical claims as current facts.

Quotation strategy:

- Ask Eurocircuits to source exact MPNs where available.
- If it cannot source an exact part, identify customer-supplied options before
  buying. Likely attention items from the handover are
  `ATX-12-F-26.000MHz-F05-T`, `TC1-1-13M+`, `0402CS-5N6XGLW`.
- The handover proposes Johanson migrations `2450FB15A050E` to
  `2450FB15A0050001E` and `0896BM15E0025E` to `0896BM15E0025001E`.
  These are NOT applied to this BOM. Identity still needs documented review.
- For U25, use exact MPN `SG-8018CG 125.0000M-TJHSA3`. The `TJHPA3` text in
  the original description/comment/PnP was reviewed and is treated as stale
  metadata, not as an alternate or authorized substitution.
- Populate J7 with exact MPN `CONUFL001-SMD-T`. Leave R25, C108 and C122 DNP;
  they isolate J7 electrically in the released configuration.
- Do not buy a second 26 MHz oscillator for U29: the Full schematic explicitly
  marks that optional oscillator block as not fitted.
- Do not source or quote SH1/SH2 shield cans. They are intentionally not fitted
  and are outside the reproduction and quotation scope.

Quote component sources, lead times, spares and customer-supply requirements.
No parts have been ordered or reserved by preparing this package.
