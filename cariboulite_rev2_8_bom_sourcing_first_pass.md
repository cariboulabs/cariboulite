# CaribouLite Rev 2.8 Full — BOM sourcing first pass

Historical sourcing snapshot. The
[manufacturer-direct follow-up](cariboulite_rev2_8_bom_sourcing_manufacturer_update.md)
supersedes this pass, particularly the L3 obsolescence assessment. Stock and
lifecycle observations below refer to 2026-09-12 and were not rechecked in the
2026-10-16 documentation review. The
[quotation package](manufacturing/rev2.8-full-eurocircuits/README.md) records the
current BOM decisions and unresolved fabrication holds; no order is authorized
by this snapshot.

**Date checked:** 2026-09-12  
**Source BOM:** `cariboulite_bom.xlsx` from the public CaribouLite Rev 2.8 production package  
**Build assumption:** 5 assembled Full boards  
**Reproduction policy:** For the first reproduction run, use the **exact BOM manufacturer part number (MPN)**. No engineering substitutions are approved in this document. Where the BOM itself lists multiple acceptable MPNs, those are treated as original-design alternatives.

## Status legend

- 🟢 **Stocked / Active / Listed** — no immediate sourcing concern found.
- 🟡 **Verify** — exact MPN needs a fresh distributor check before ordering, but no confirmed showstopper yet.
- 🟠 **Attention** — backorder, revised ordering code, long lead time, or exact stock issue.
- 🔴 **High risk** — exact BOM MPN is obsolete or otherwise a clear reproduction risk.

## Line-by-line BOM

| # | Designator(s) | Exact BOM MPN | Qty/board | Qty for 5 | Value / role | Status | First-pass result | Risk | Source |
|---:|---|---|---:|---:|---|---|---|---|---|
| 1 | C1, C26, C33, C66, C79, C121, C127 | `GRJ155R61A475ME11J` | 7 | 35 | 4.7u | 🟡 Verify | Exact MPN not independently stock-verified in this pass; ordinary 0402 4.7 µF MLCC, but exact-MPN policy applies. | Low | — |
| 2 | C3, C14, C21, C23, C27, C28, C30 | `LMK105BJ105MVHF` | 7 | 35 | 1u | 🟡 Verify | Exact MPN not independently stock-verified in this pass; ordinary 0402 1 µF MLCC. | Low | — |
| 3 | C5, C7, C9, C10, C17, C34, C36, C65, C68, C69, C71, C72, C73, C74, C81, C86, C89, C119, C120 | `0402YD104KAT2A` | 19 | 95 | 100n | 🟢 Listed | Exact KYOCERA AVX MPN is currently listed by Mouser. | Low | [link](https://www.mouser.com/ProductDetail/KYOCERA-AVX/0402YD104KAT2A) |
| 4 | C11 | `0402N2R2B500CT` | 1 | 5 | 2.2p | 🟢 Active | DigiKey lists the exact Walsin 2.2 pF C0G part as Active. | Low | [link](https://www.digikey.com/en/products/detail/walsin-technology-corporation/0402N2R2B500CT/9354793) |
| 5 | C12, C20 | `0402N1R8B500CT` | 2 | 10 | 1.8p | 🟠 Backorder | Mouser shows zero immediate stock but substantial quantities on order; exact MPN still orderable. | Medium | [link](https://www.mouser.com/ProductDetail/Walsin/0402N1R8B500CT) |
| 6 | C13, C15, C35, C37, C38, C41, C42, C43, C47, C48, C49, C50, C51, C54, C55, C56, C57, C60, C61, C62, C64, C70, C75, C77, C78, C82, C83, C84, C85, C90, C93, C95, C96, C99, C129, C130, C131, C132 | `GRM1555C2A102JE01D` | 38 | 190 | 1n | 🟢 Stocked | DigiKey lists exact Murata C0G part Active with large stock. | Low | [link](https://www.digikey.com/en/products/detail/murata-electronics/GRM1555C2A102JE01D/13401927) |
| 7 | C16, C24, C25, C39, C63, C87, C103, C105, C106, C107, C112, C116, C118, C125, C126 | `C0402C330J5GAC` | 15 | 75 | 33p | 🟢 Stocked | Mouser Europe shows very large stock of exact KEMET MPN. | Low | [link](https://eu.mouser.com/ProductDetail/KEMET/C0402C330J5GAC) |
| 8 | C19 | `GCM1555C1HR80WA16D` | 1 | 5 | 0.8p | 🟢 Active | DigiKey lists exact Murata 0.8 pF C0G part. | Low | [link](https://www.digikey.com/en/products/detail/murata-electronics/GCM1555C1HR80WA16D/11618809) |
| 9 | C22 | `0402N150J500CT` | 1 | 5 | 15p | 🟢 Active | Mouser/DigiKey list exact Walsin 15 pF C0G part. | Low | [link](https://www.digikey.com/en/products/detail/walsin-technology-corporation/0402N150J500CT/6707541) |
| 10 | C31, C32, C67, C102, C111, C115, C117 | `GRM1555C1E103JE01J` | 7 | 35 | 10n | 🟢 Stocked | DigiKey lists exact Murata C0G part Active with large stock. | Low | [link](https://www.digikey.nl/en/products/detail/murata-electronics/GRM1555C1E103JE01J/16034186) |
| 11 | C45, C46, C58, C59, L8, L17, R32 | `CRCW04020000Z0EDC` | 7 | 35 | 0 Ohms | 🟢 Stocked | DigiKey NL shows large stock of exact Vishay 0 Ω jumper. | Low | [link](https://www.digikey.nl/en/products/detail/vishay-dale/CRCW04020000Z0EDC/7928484) |
| 12 | C76, C88, C91, C94, C100, C101 | `C0402C101J5GACTU` | 6 | 30 | 100p | 🟢 Stocked | Mouser NL shows very large stock; DigiKey also lists exact part. | Low | [link](https://nl.mouser.com/en/ProductDetail/KEMET/C0402C101J5GACTU) |
| 13 | C109, C110 | `GRM1555C1H331JA01D` | 2 | 10 | 330p | 🟢 Stocked | DigiKey shows large stock of exact Murata 330 pF C0G part. | Low | [link](https://www.digikey.com/en/products/detail/murata-electronics/GRM1555C1H331JA01D/587206) |
| 14 | C113 | `0402N181F500CT` | 1 | 5 | 180p | 🟡 NRND | Exact Walsin MPN is stocked at DigiKey but marked not recommended for new design. For reproduction, buy exact part while available. | Medium | [link](https://www.digikey.com/en/products/detail/walsin-technology-corporation/0402N181F500CT/9355271) |
| 15 | C114 | `04025A8R2BAT2A` | 1 | 5 | 8.2p | 🟡 Verify | Exact MPN not independently stock-verified in this pass. RF matching capacitor: do not substitute. | Medium | — |
| 16 | D1, D2, D7, D8, D9, D10, D11, D12 | `LTST-C190KRKT` | 8 | 40 | Red 631nm LED Indication - Discrete 2V 0603 | 🟢 Stocked | Exact Lite-On red LED is Active and very widely stocked. | Low | [link](https://nl.mouser.com/en/ProductDetail/LITEON/LTST-C190KRKT) |
| 17 | D3, D4 | `0402ESDB-MLP1` | 2 | 10 | ESD Suppressors / TVS Diodes ESD Protection Device, … | 🟢 Stocked | Exact Eaton ESD suppressor is available through Mouser according to current distributor aggregation. | Low | [link](https://octopart.com/part/eaton/0402ESDB-MLP1) |
| 18 | J1 | `M20-7832046, PPPC202LFBN-RC, 61304021821` | 1 | 5 | RPI 40-pin 2.54mm header, 2-rows, socket | 🟢 BOM alternatives | BOM itself lists three acceptable header MPNs. Harwin M20-7832046 is currently stocked at Mouser. | Low | [link](https://eu.mouser.com/en/ProductDetail/Harwin/M20-7832046) |
| 19 | J2, J3 | `CON-SMA-EDGE-S` | 2 | 10 | RF SMA Connectors / Coaxial Connectors SMA Female PC… | 🟠 Backorder | Exact RF Solutions SMA edge connector is Active but DigiKey currently shows no immediate stock. | Medium | [link](https://www.digikey.nl/en/products/detail/rf-solutions/CON-SMA-EDGE-S/5845767) |
| 20 | J6 | `10129383-912001ALF` | 1 | 5 | Headers & Wire Housings ECONOSTIK HEADER DR VT SMT 2… | 🟢 Listed | Exact Amphenol FCI 2x6 SMT header is currently listed by Mouser and DigiKey. | Low | [link](https://www.digikey.com/en/products/detail/amphenol-cs-fci/10129383-912001ALF/7916195) |
| 21 | J7 | `CONUFL001-SMD-T` | 1 | 5 | RF Connectors / Coaxial Connectors U.FL Straight Sur… | 🟢 Stocked elsewhere | Exact TE Connectivity U.FL-style connector is currently stocked by LCSC; verify Eurocircuits sourcing route. | Medium | [link](https://www.lcsc.com/product-detail/C22418213.html) |
| 22 | L1, L2, L4, L5, L13, L14, L15, L18, L20, L21 | `BLM15HD182SN1D` | 10 | 50 | Ferrite Beads 1800ohms GHz HiSpd | 🟢 Stocked | Exact Murata ferrite bead is Active and stocked at DigiKey. | Low | [link](https://www.digikey.com/en/products/detail/murata-electronics/BLM15HD182SN1D/1948302) |
| 23 | L3 | `0402CS-5N6XGLW` | 1 | 5 | 5.6n | 🔴 Obsolete | Mouser explicitly marks this exact Coilcraft 5.6 nH RF inductor Obsolete. This is a genuine exact-reproduction sourcing risk. | High | [link](https://www.mouser.com/ProductDetail/Coilcraft/0402CS-5N6XGLW) |
| 24 | R1, R6, R7, R33, R34, R47, R48, R49, R50, R51, R52 | `CRCW0402470RFKED` | 11 | 55 | 470 | 🟡 Verify | Exact MPN not independently stock-verified in this pass; standard Vishay 0402 resistor. | Low | — |
| 25 | R2, R8, R9, R11, R16, R30 | `RC0402FR-07102KL` | 6 | 30 | 102k | 🟢 Stocked | Exact Yageo resistor is Active and stocked at Mouser/DigiKey. | Low | [link](https://www.mouser.com/ProductDetail/YAGEO/RC0402FR-07102KL) |
| 26 | R3, R5, R14, R15, R27, R28, R35, R43, R44 | `CRCW040210K0FKEDC` | 9 | 45 | 10k | 🟢 Listed | Exact Vishay 10 kΩ 0402 MPN is currently listed by Mouser. | Low | [link](https://www.mouser.com/en/ProductDetail/Vishay/CRCW040210K0FKEDC) |
| 27 | R13, R17, R18, R19, R20, R22, R24 | `CRCW0402100RFKEDC` | 7 | 35 | 100 | 🟢 Active | Exact Vishay 100 Ω resistor is listed at DigiKey. | Low | [link](https://www.digikey.com/en/products/detail/vishay-dale/CRCW0402100RFKEDC/7928384) |
| 28 | R21, R23 | `CRCW0402162RFKED` | 2 | 10 | 162 | 🟢 Stocked | Exact Vishay 162 Ω resistor is stocked by Mouser. | Low | [link](https://www.mouser.com/en/ProductDetail/Vishay/CRCW0402162RFKED) |
| 29 | R36 | `CRCW040222K0FKEDC` | 1 | 5 | 22k | 🟡 Verify | Exact MPN not independently stock-verified in this pass; standard Vishay 22 kΩ 0402 resistor. | Low | — |
| 30 | R42, R45, R46 | `CRCW04021K00FKEDC` | 3 | 15 | 1k | 🟡 Verify | Exact MPN not independently stock-verified in this pass; standard Vishay 1 kΩ 0402 resistor. | Low | — |
| 31 | S1 | `EVQ9P701P` | 1 | 5 | Tactile Switches SMD 3.5X2.9MM SID-OP L SHAPED TERM … | 🟢 Current manufacturer page | Panasonic still publishes the exact EVQ9P701P side-operated SMD switch. | Low | [link](https://industry.panasonic.com/global/en/products/control/switch/light-touch/number/evq9p701p) |
| 32 | U1 | `AT86RF215-ZU` | 1 | 5 | RF Transceiver Dual Band IEEE 802.15.4 TRX, 48QFN | 🟢 Stocked | Exact Microchip tray part is in stock at Mouser; active production path remains healthy. | Low | [link](https://www.mouser.com/ProductDetail/Microchip-Technology/AT86RF215-ZU) |
| 33 | U2 | `2450FB15A050E` | 1 | 5 | 2.45 GHz EIA 0805 Balun / Filter Combination 50 / 50… | 🟠 MPN revised | Mouser marks this as a previous Johanson part number and points to global MPN 2450FB15A0050001E. For first build, confirm manufacturer equivalence before accepting revised code. | Medium | [link](https://eu.mouser.com/ProductDetail/Johanson-Technology/2450FB15A050E) |
| 34 | U3 | `ATX-12-F-26.000MHz-F05-T` | 1 | 5 | 26 MHz | 🟠 Tight supply | Exact Abracon TCXO remains listed/current, but ordinary distributor stock is limited/variable. This is clock-critical: exact MPN only. | High | [link](https://www.mouser.com/en/ProductDetail/ABRACON/ATX-12-F-26.000MHz-F05-T) |
| 35 | U4 | `0896BM15E0025E` | 1 | 5 | RF Balun 863MHz ~ 928MHz 50 / 50Ohm 0805 (2012 Metri… | 🟠 MPN revised | Mouser marks this as a previous Johanson part number and points to global MPN 0896BM15E0025001E. Confirm that the revision is electrically/mechanically identical before use. | Medium | [link](https://www.mouser.com/en/ProductDetail/Johanson-Technology/0896BM15E0025E) |
| 36 | U5 | `NCV8187AMT330TAG` | 1 | 5 | LDO Voltage Regulators 1.2A 3.3V Active discharge | 🟢 Stocked | Exact onsemi 3.3 V LDO is well stocked at Mouser. | Low | [link](https://www.mouser.com/ProductDetail/onsemi/NCV8187AMT330TAG) |
| 37 | U6 | `TLV7111225DSER` | 1 | 5 | LDO Voltage Regulators Dual 200mA Output Lo Noise Hi… | 🟡 Verify | Exact TI dual-output LDO MPN not independently stock-verified in this pass. | Medium | — |
| 38 | U9, U15 | `MAAM-011229` | 2 | 10 | RF Amplifier .05-4GHz NF 2.2dBmax -40C +85C | 🟢 Stocked | Exact base device is Active; DigiKey stocks MAAM-011229-TR1000 in large quantity. | Low | [link](https://www.digikey.nl/en/products/detail/macom-technology-solutions/MAAM-011229-TR1000/6575479) |
| 39 | U10 | `LFCN-2250+` | 1 | 5 | LTCC Low Pass Filter, DC - 2200 MHz, 50O | 🟢 Stocked | Exact Mini-Circuits low-pass filter is Active and stocked by DigiKey/Mouser. | Low | [link](https://www.digikey.nl/en/products/detail/mini-circuits/LFCN-2250/13680762) |
| 40 | U11, U18 | `SKY13373-460LF` | 2 | 10 | RF Switch ICs .1-6.0GHz SP3T IL .45dB @ 2.5GHz | 🟢 Stocked | Exact Skyworks SP3T RF switch is Active and very widely stocked. | Low | [link](https://www.digikey.com/en/products/detail/skyworks-solutions-inc/SKY13373-460LF/5015832) |
| 41 | U12 | `HFCN-2275+` | 1 | 5 | Signal Conditioning HI PASS FLTR / SURF MT / RoHS | 🟢 Listed | Exact Mini-Circuits high-pass filter is currently listed by Mouser. | Low | [link](https://www.mouser.com/en/ProductDetail/Mini-Circuits/HFCN-2275%2B) |
| 42 | U13, U14 | `CG2164X3-C2` | 2 | 10 | RF Switch ICs 2.5 and 6GHz DPDT 802.11a/b/g/n/ac | 🟢 Stocked | Exact CEL RF switch is stocked at Mouser. | Low | [link](https://www.mouser.com/ProductDetail/CEL/CG2164X3-C2) |
| 43 | U16 | `ICE40LP1K-QN84` | 1 | 5 | FPGA - Field Programmable Gate Array iCE40LP 1280 LU… | 🟢 Stocked / long lead | Exact Lattice FPGA is in stock at Mouser; factory lead time is long, so secure prototype quantity early. | Medium | [link](https://eu.mouser.com/ProductDetail/Lattice/iCE40LP1K-QN84) |
| 44 | U19, U20 | `CG2409X3-C2` | 2 | 10 | RF Switch ICs 50-6000MHz SPDT 802.11a/b/g/n/ac | 🟡 Verify | Exact CEL MPN was identified in the earlier pass, but current exact stock was not independently re-verified in this line-by-line pass. | Medium | — |
| 45 | U21 | `TC4-19G2+` | 1 | 5 | Audio Transformers / Signal Transformers TC XFMR / S… | 🟢 Stocked | Exact Mini-Circuits transformer is Active; Mini-Circuits shows substantial direct stock. | Low | [link](https://www.minicircuits.com/WebStore/dashboard.html?model=TC4-19G2%2B) |
| 46 | U22 | `RFFC5072TR13` | 1 | 5 | RF Mixer LO Freq. 85-4200MHz WB Synthesizer/VCO | 🟢 Stocked | Exact Qorvo mixer/synthesizer is Active and stocked at DigiKey. | Low | [link](https://www.digikey.com/en/products/detail/qorvo/RFFC5072TR13/24611156) |
| 47 | U23 | `TC1-1-13M+` | 1 | 5 | Transformers / Signal Transformers TC XFMR / SURF MO… | 🟠 No current DK stock | Exact Mini-Circuits balun is Active but DigiKey currently shows zero stock with incoming inventory. Check Mini-Circuits direct before ordering. | High | [link](https://www.digikey.com/en/products/detail/mini-circuits/TC1-1-13M/13927704) |
| 48 | U25 | `SG-8018CG 125.0000M-TJHSA3` | 1 | 5 | Standard Clock Oscillators SG-8018CG 125.0000M-TJHPA… | 🟢 Previously verified | Exact Epson 125 MHz oscillator was verified as stocked in the initial sourcing pass; re-check immediately before purchase. | Low | — |
| 49 | U26 | `CAT24C32HU4I-GT3` | 1 | 5 | EEPROM Serial-I2C 32K 4Kx8 1.8V/5V UDFN8 CAT24C32HU4… | 🟢 Stocked | Exact onsemi EEPROM is Active and stocked by DigiKey/Mouser. | Low | [link](https://www.digikey.com/en/products/detail/onsemi/CAT24C32HU4I-GT3/4927901) |
| 50 | U27, U28 | `MMBT3904,215` | 2 | 10 | Bipolar Transistors - BJT TRANS BIPOLAR | 🟠 Active / no stock | Exact Nexperia MPN is Active but currently out of stock at both DigiKey and Mouser; many direct substitutes exist, but exact-build policy means source the stated MPN or defer. | Medium | [link](https://www.digikey.com/en/products/detail/nexperia-usa-inc/MMBT3904-215/1944320) |

## Immediate procurement flags

1. **`0402CS-5N6XGLW` (L3)** — the exact Coilcraft 5.6 nH RF inductor is marked **Obsolete** by Mouser. This is the clearest exact-reproduction problem found so far. Because it is an RF matching component, we should not silently replace it with a same-value inductor.
2. **`ATX-12-F-26.000MHz-F05-T` (U3)** — exact production TCXO. Supply is tighter than most of the BOM and this part is clock-critical. Secure exact devices from a traceable source before PCB assembly.
3. **`TC1-1-13M+` (U23)** — Active, but DigiKey currently has zero stock. Check Mini-Circuits direct and other authorized distribution.
4. **`2450FB15A050E` (U2) and `0896BM15E0025E` (U4)** — Johanson has revised the ordering numbers. We need written/data-sheet confirmation that the new global MPNs are electrically, mechanically, and process-identical before treating them as exact-reproduction parts.
5. **`ICE40LP1K-QN84` (U16)** — available now, but factory lead time is long. Buy prototype quantity early.
6. **`MMBT3904,215` (U27/U28)** — exact Nexperia code is Active but presently out of stock at the main distributors checked. This is electrically non-critical compared with the RF path, but the first-build policy still says no substitutions.

## RF / clock parts that should be frozen to exact MPN

For the initial reproduction, the following deserve an explicit **DO NOT SUBSTITUTE** note to Eurocircuits even if their BOM tool proposes equivalents:

`0402N2R2B500CT`, `0402N1R8B500CT`, `GCM1555C1HR80WA16D`, `0402N150J500CT`, `0402N181F500CT`, `04025A8R2BAT2A`, `0402CS-5N6XGLW`, `AT86RF215-ZU`, `2450FB15A050E`, `ATX-12-F-26.000MHz-F05-T`, `0896BM15E0025E`, `MAAM-011229`, `LFCN-2250+`, `SKY13373-460LF`, `HFCN-2275+`, `CG2164X3-C2`, `CG2409X3-C2`, `TC4-19G2+`, `RFFC5072TR13`, `TC1-1-13M+`, and `SG-8018CG 125.0000M-TJHSA3`.

## Overall first-pass conclusion

The Rev 2.8 BOM still looks **buildable**, but the sourcing problem has narrowed to a small set of exact-MPN issues rather than the major RF ICs. The AT86RF215, RFFC5072, iCE40 FPGA, MACOM amplifiers, Skyworks switches, Mini-Circuits filters/transformer, regulators, EEPROM, and many passives remain obtainable. The next sourcing work should concentrate first on the obsolete Coilcraft inductor, the exact Abracon TCXO, the TC1-1-13M+ balun, the two Johanson revised MPNs, and any remaining rows marked Verify.

> Stock is time-sensitive. This document is a sourcing snapshot, not a reservation of inventory. Re-check quantities and lifecycle immediately before placing the Eurocircuits order.
