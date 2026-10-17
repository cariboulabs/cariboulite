#!/usr/bin/env python3
"""Build auditable quotation files; never edits the original hardware files.

Run from any directory in the original repository. Dependencies: requirements.txt.
The package is a technical quotation input, not a manufacturing release.
"""
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

import openpyxl
import xlrd

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'hardware/rev2'
OUT = ROOT / 'manufacturing/rev2.8-full-eurocircuits'
PREFIX = 'cariboulite_r2.8'
DX_MIL, DY_MIL = 12697.26, 7261.81
IGNORED_METADATA_NAMES = {'.DS_Store'}
EXCLUDED = {
    'R10': 'DNP: Full schematic page 2, crossed out and marked DNP',
    'R12': 'DNP: Full schematic page 2, crossed out and marked DNP',
    'L19': 'DNP: Full schematic page 6, crossed out in DO NOT PLACE block',
    'U29': 'DNP: Full schematic page 6, crossed out in DO NOT PLACE block',
    'TP13': 'Bare PCB test point: Full schematic page 3; PnP footprint TP_40; not a purchased component',
}
LAYERS = {
    'GTL': ('Copper 1 / Component Side', '1'),
    'G1': ('Copper 2 / Layer 1', '2'),
    'G2': ('Copper 3 / Layer 2', '3'),
    'GBL': ('Copper 4 / Solder Side', '4'),
    'GTO': ('Top silkscreen / Top Overlay', ''),
    'GBO': ('Bottom silkscreen / Bottom Overlay', ''),
    'GTS': ('Top solder mask / Top Solder', ''),
    'GBS': ('Bottom solder mask / Bottom Solder', ''),
    'GTP': ('Top solder paste', ''),
    'GBP': ('Bottom solder paste', ''),
    'Outline': ('Board outline; duplicated source contours preserved', ''),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_csv(path, fields, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')


def zip_files(path, entries):
    """Stable archive timestamps, ordering and permissions."""
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(entries):
            info = zipfile.ZipInfo(name, (2026, 9, 12, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, data)


def parse_pnp():
    p = SOURCE / 'assembly/full_pnp/cariboulite_full_pnp.txt'
    records = []
    for line_number, line in enumerate(p.read_text().splitlines(), 1):
        if not line.strip() or line.startswith('Designator '):
            continue
        t = line.split(maxsplit=10)
        assert len(t) == 11, (line_number, line)
        assert all(v.endswith('mil') for v in t[2:8])
        assert t[8] in ('T', 'B')
        records.append(dict(zip(
            ['Designator', 'Footprint', 'Mid X mil', 'Mid Y mil', 'Ref X mil',
             'Ref Y mil', 'Pad X mil', 'Pad Y mil', 'Side', 'Rotation', 'Comment'],
            [t[0], t[1], *[float(v[:-3]) for v in t[2:8]], t[8], float(t[9]), t[10]]),
            **{'Source Line': line_number}))
    assert len(records) == len({r['Designator'] for r in records}) == 210
    return records


def gerber_flashes(path):
    """Read explicit D03 coordinates from this source's absolute inch 2:5 export.

    Used only as an alignment check, not a general Gerber/CAM parser.
    """
    s = path.read_text()
    assert '%FSLAX25Y25*%' in s and '%MOIN*%' in s
    x = y = 0
    points = []
    for command in s.split('*'):
        command = command.strip()
        if not re.match(r'^[XYD]', command):
            continue
        mx, my = re.search(r'X(-?\d+)', command), re.search(r'Y(-?\d+)', command)
        if mx:
            x = int(mx[1]) / 100
        if my:
            y = int(my[1]) / 100
        if 'D03' in command:
            points.append((x, y))
    return points


def drill_summary():
    summaries = []
    header_points = []
    for ext, span, expected in [('TXT', '1-4', {2: 805, 3: 4, 4: 2, 5: 2, 6: 40, 7: 3}),
                                ('TX1', '1-2', {1: 661}), ('TX2', '3-4', {1: 736})]:
        path = SOURCE / 'pcb/ncdrill' / f'{PREFIX}.{ext}'
        s = path.read_text()
        assert ';FILE_FORMAT=2:5' in s and 'INCH,LZ' in s
        tools, hits, plating = {}, {}, None
        tool = None
        x = y = 0
        for line in s.splitlines():
            if line == ';TYPE=PLATED':
                plating = 'PTH'
            elif line == ';TYPE=NON_PLATED':
                plating = 'NPTH'
            m = re.match(r'T(\d+)F\d+S\d+C([\d.]+)$', line)
            if m:
                tools[int(m[1])] = (float(m[2]), plating)
            if re.fullmatch(r'T\d+', line):
                tool = int(line[1:])
            if re.match(r'^[XY]', line):
                hits[tool] = hits.get(tool, 0) + 1
                mx, my = re.search(r'X(-?\d+)', line), re.search(r'Y(-?\d+)', line)
                if mx:
                    x = int(mx[1]) / 100
                if my:
                    y = int(my[1]) / 100
                if ext == 'TXT' and tool == 6:
                    header_points.append((x, y))
        assert hits == expected, (ext, hits)
        for tool, count in hits.items():
            diameter, plated = tools[tool]
            summaries.append({'File': path.name, 'Tool': tool, 'Copper span': span,
                              'Plating': plated, 'Diameter inch': diameter,
                              'Diameter mm': round(diameter * 25.4, 6), 'Hits': count})
    assert len(header_points) == 40
    return summaries, (sum(x for x, _ in header_points) / 40,
                       sum(y for _, y in header_points) / 40)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    # Finder metadata is host-local, not production source. Remove stale copies
    # from a prior macOS run so Linux and macOS builds have identical inputs.
    for p in OUT.rglob('*'):
        if p.is_file() and p.name in IGNORED_METADATA_NAMES:
            p.unlink()
    # Inventory all revision-2 sources, explicitly excluding the ISM variant from delivery.
    manifest = []
    for p in sorted(SOURCE.rglob('*')):
        if not p.is_file() or p.name in IGNORED_METADATA_NAMES:
            continue
        rel = p.relative_to(SOURCE)
        excluded = any('ism' in part.lower() for part in rel.parts)
        target = OUT / 'originals' / rel
        if not excluded:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, target)
            assert digest(p) == digest(target)
        manifest.append({'source': str(p.relative_to(ROOT)), 'bytes': p.stat().st_size,
                         'sha256': digest(p), 'included': not excluded,
                         'package_path': '' if excluded else str(target.relative_to(OUT)),
                         'note': 'Excluded: ISM variant' if excluded else 'Unmodified original'})
    write_json(OUT / 'source_manifest.json', manifest)
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    text = '# Source manifest\n\nRepository commit: `' + commit + '`\n\n'
    text += 'Hashes describe file contents at package generation; ISM variant files are inventoried but excluded.\n\n'
    text += '| Source | Bytes | SHA-256 | Included |\n|---|---:|---|---|\n'
    for r in manifest:
        text += f"| `{r['source']}` | {r['bytes']} | `{r['sha256']}` | {r['included']} |\n"
    (OUT / 'source_manifest.md').write_text(text)

    pnp = parse_pnp()
    indexed = {r['Designator']: r for r in pnp}
    workbook = openpyxl.load_workbook(SOURCE / 'assembly/full_bom/cariboulite_bom.xlsx', data_only=True)
    assert workbook.active.max_row == 51
    bom, audit, refs = [], [], set()
    for number, row in enumerate(list(workbook.active.values)[1:], 2):
        designators, mpn, qty, value, description, footprint, comment = row[:7]
        names = [n.strip() for n in designators.split(',')]
        assert len(names) == qty and not refs.intersection(names)
        refs.update(names)
        notes = 'Exact MPN; no automatic substitution.'
        quote_mpn = mpn
        if names == ['J1']:
            quote_mpn = mpn.split(',')[0].strip()
            notes += ' First original BOM alternative selected for quotation; verify mounting side and mating height.'
        if names == ['U25']:
            notes += (' RESOLVED: use primary MPN SG-8018CG 125.0000M-TJHSA3. '
                      'The schematic and primary BOM MPN agree on the standby (S) variant; '
                      'TJHPA3 in the source description/comment/PnP is treated as stale metadata. '
                      'Pin 1 is tied high, but the P variant is not an approved substitution.')
        if names == ['J7']:
            notes += (' RESOLVED: populate J7 with exact MPN CONUFL001-SMD-T. '
                      'The BOM, PnP, assembly drawing and physical reference board agree; '
                      'J7 is outside the schematic DO NOT PLACE boundary. '
                      'Leave R25, C108 and C122 DNP so J7 remains electrically isolated.')
        if names == ['U21']:
            notes += ' Description mentions TC4-19+; that alternative is NOT selected.'
        entry = {'Reference Designators': designators, 'MPN': quote_mpn, 'Quantity': int(qty),
                 'Value': value or '', 'Description': description, 'Package': footprint,
                 'Notes': notes}
        bom.append(entry)
        audit.append(dict(entry, **{'Original MPN': mpn, 'Original Comment': comment or '',
                                   'Source Row': number, 'Quantity for 5': int(qty) * 5,
                                   'Manufacturer': '', 'Sourcing Status': 'Not reverified; exact MPN required',
                                   'Lifecycle': 'Not reverified', 'Supplier': '',
                                   'Customer Supplied': 'TBD', 'Substitution Allowed': 'NO'}))
    assert len(refs) == 205 and set(indexed) - refs == set(EXCLUDED) and not refs - set(indexed)
    write_csv(OUT / 'upload/02-full-bom.csv', list(bom[0]), bom)
    write_csv(OUT / 'procurement/bom-audit.csv', list(audit[0]), audit)
    write_csv(OUT / 'validation/pnp-original-parsed.csv', list(pnp[0]), pnp)
    excluded_rows = [{'Designator': n, 'Reason': reason} for n, reason in EXCLUDED.items()]
    write_csv(OUT / 'validation/pnp-exclusions.csv', list(excluded_rows[0]), excluded_rows)

    drills, header_center = drill_summary()
    write_csv(OUT / 'validation/drill-tools.csv', list(drills[0]), drills)
    flashes = {side: gerber_flashes(SOURCE / 'pcb/gerber' / f'{PREFIX}.{ext}')
               for side, ext in [('T', 'GTL'), ('B', 'GBL')]}
    alignment = []
    for r in pnp:
        x, y = r['Pad X mil'] + DX_MIL, r['Pad Y mil'] + DY_MIL
        residual = min(max(abs(x-a), abs(y-b)) for a, b in flashes[r['Side']])
        assert residual < 0.1, (r['Designator'], residual)
        alignment.append({'Designator': r['Designator'], 'Side': r['Side'],
                          'Gerber Pad X mil': x, 'Gerber Pad Y mil': y,
                          'Max-axis Residual mil': round(residual, 6)})
    write_csv(OUT / 'validation/pad-alignment.csv', list(alignment[0]), alignment)
    assert abs(header_center[0] - (indexed['J1']['Ref X mil'] + DX_MIL)) < 0.01
    assert abs(header_center[1] - (indexed['J1']['Ref Y mil'] + DY_MIL)) < 0.01
    cpl, transforms = [], []
    for r in pnp:
        ref = r['Designator']
        if ref not in refs:
            continue
        x, y = r['Mid X mil'] + DX_MIL, r['Mid Y mil'] + DY_MIL
        method = 'Source Mid X/Y + verified common translation'
        if ref == 'J1':
            x, y = header_center
            method = 'Centroid of 40 T6 drilled header holes; agrees with source Ref X/Y + translation'
        cpl.append({'Designator': ref, 'X (mm)': f'{x * 0.0254:.6f}',
                    'Y (mm)': f'{y * 0.0254:.6f}', 'Rotation': f"{r['Rotation'] % 360:.2f}",
                    'Side': {'T': 'Top', 'B': 'Bottom'}[r['Side']]})
        transforms.append({'Designator': ref, 'Source Line': r['Source Line'], 'Method': method,
                           'Source Rotation': r['Rotation'], 'Output Rotation': r['Rotation'] % 360,
                           'Source Side': r['Side']})
    write_csv(OUT / 'upload/03-full-cpl-mm.csv', list(cpl[0]), cpl)
    write_csv(OUT / 'validation/cpl-transform-audit.csv', list(transforms[0]), transforms)

    stack = xlrd.open_workbook(SOURCE / 'pcb/stack/cariboulite_r2.8.xls').sheet_by_index(0)
    stack_rows = [{'Source Row': i+1, 'Name': stack.cell_value(i, 6),
                   'Material': stack.cell_value(i, 7), 'Thickness': stack.cell_value(i, 8),
                   'Dielectric Constant': stack.cell_value(i, 9)} for i in range(3, 16)]
    write_csv(OUT / 'validation/stackup-source.csv', list(stack_rows[0]), stack_rows)
    layer_rows = [{'File': f'{PREFIX}.{ext}', 'Function': role, 'Physical Copper Order': order,
                   'Evidence': 'Original .EXTREP; copper header Layer_Physical_Order'}
                  for ext, (role, order) in LAYERS.items()]
    for ext, (_, order) in LAYERS.items():
        if order:
            data = (SOURCE / 'pcb/gerber' / f'{PREFIX}.{ext}').read_text()
            assert f'Layer_Physical_Order={order}*' in data
    write_csv(OUT / 'validation/layer-map.csv', list(layer_rows[0]), layer_rows)
    pcb_entries = [(f'{PREFIX}.{ext}', (SOURCE / 'pcb/gerber' / f'{PREFIX}.{ext}').read_bytes())
                   for ext in LAYERS]
    pcb_entries += [(f'{PREFIX}.{ext}', (SOURCE / 'pcb/ncdrill' / f'{PREFIX}.{ext}').read_bytes())
                    for ext in ['TXT', 'TX1', 'TX2']]
    pcb_entries += [('READ-ME-FIRST.txt',
                     ('QUOTATION / FEASIBILITY ONLY. Full Rev 2.8, quantity 5.\n'
                      'Blind vias: TX1 spans copper 1-2 (661 x 0.1016 mm); TX2 spans 3-4 (736 x 0.1016 mm).\n'
                      'TXT spans 1-4: T2-T6 plated, T7 non-plated. Do not merge the blind drills into through drills.\n'
                      'Copper order GTL, G1, G2, GBL. GTS/GBS mask; GTO/GBO legend; GTP/GBP paste.\n'
                      'Original Gerber/Excellon inch 2:5 absolute coordinates preserved.\n'
                      'Read supporting technical-review.md for stackup, finish, outline and assembly holds.\n'
                      'No PCB modifications or automatic component substitutions authorized.\n').encode())]
    zip_files(OUT / 'upload/01-pcb-gerber-drill.zip', pcb_entries)
    validation = {'source_commit': commit, 'bom_lines': len(bom), 'components_per_board': len(refs),
                  'components_for_five_excluding_spares': len(refs)*5,
                  'source_pnp_rows': len(pnp), 'upload_cpl_rows': len(cpl),
                  'cpl_top': sum(r['Side'] == 'Top' for r in cpl),
                  'cpl_bottom': sum(r['Side'] == 'Bottom' for r in cpl),
                  'excluded_pnp': EXCLUDED, 'pad_matches': len(alignment),
                  'alignment_translation_mil': [DX_MIL, DY_MIL],
                  'max_alignment_residual_mil': max(r['Max-axis Residual mil'] for r in alignment),
                  'j1_center_gerber_mil': list(header_center),
                  'drill_hits': sum(r['Hits'] for r in drills), 'blind_drill_hits': 1397,
                  'original_copies_verified': sum(r['included'] for r in manifest),
                  'status': 'LOCAL CHECKS PASSED; TECHNICAL QUOTATION ONLY; MANUFACTURER PREFLIGHT NOT RUN'}
    write_json(OUT / 'validation/summary.json', validation)
    # Package the reproducibility tool; its normal invocation expects the original repository layout.
    (OUT / 'tools').mkdir(exist_ok=True)
    shutil.copyfile(__file__, OUT / 'tools/build_rev2_8_package.py')
    shutil.copyfile(ROOT / 'manufacturing/requirements.txt', OUT / 'tools/requirements.txt')
    files = sorted(p for p in OUT.rglob('*') if p.is_file() and p.name not in
                   IGNORED_METADATA_NAMES | {'SHA256SUMS', 'cariboulite-rev2.8-full-quotation.zip'})
    (OUT / 'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.relative_to(OUT).as_posix()}\n' for p in files))
    files.append(OUT / 'SHA256SUMS')
    zip_files(OUT / 'cariboulite-rev2.8-full-quotation.zip',
              [(p.relative_to(OUT).as_posix(), p.read_bytes()) for p in files])
    with zipfile.ZipFile(OUT / 'cariboulite-rev2.8-full-quotation.zip') as z:
        assert z.testzip() is None
    print(json.dumps(validation, indent=2))


if __name__ == '__main__':
    main()
