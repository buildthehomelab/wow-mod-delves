#!/usr/bin/env python3
"""Add a converted Earthen minecart display to full baseline client DBCs."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from build_pilot_client import Dbc

SCHEMAS = {'CreatureModelData.dbc': 28, 'CreatureDisplayInfo.dbc': 16}


def bits(value):
    if not math.isfinite(value):
        raise ValueError('Non-finite model bounds')
    return struct.unpack('<I', struct.pack('<f', value))[0]


def build(source, baseline, client, model_path, model_id, display_id, output):
    if output.exists() or any(output.resolve().is_relative_to(path.resolve()) for path in (source, client)):
        raise ValueError('Output must be new and outside input directories')
    relative = Path(model_path.replace('\\', '/'))
    if relative.is_absolute() or '..' in relative.parts or relative.suffix.lower() != '.m2':
        raise ValueError('Invalid converted model path')
    model = client / relative
    if not model.resolve().is_relative_to(client.resolve()):
        raise ValueError('Model escapes client input')
    data = model.read_bytes()
    if data[:8] != b'MD20' + struct.pack('<I', 264):
        raise ValueError('Expected converted MD20 version264')
    count, offset = struct.unpack_from('<II', data, 0x3c)
    if not count or count > 65535 or offset < 0x130 or offset + count * 48 > len(data):
        raise ValueError('Invalid converted vertex table')
    points = [struct.unpack_from('<3f', data, offset + index * 48) for index in range(count)]
    if not all(math.isfinite(v) for point in points for v in point):
        raise ValueError('Non-finite vertices')
    low = [min(point[k] for point in points) for k in range(3)]
    high = [max(point[k] for point in points) for k in range(3)]
    if any(high[k] <= low[k] for k in range(3)) or high[2] <= 0:
        raise ValueError('Degenerate model bounds')
    if not 0 < model_id <= 1000000 or not 0 < display_id <= 1000000:
        raise ValueError('Invalid provisional display/model IDs')
    expected = json.loads(baseline.read_text())
    tables = {}
    for name, fields in SCHEMAS.items():
        raw = (source / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected[name]:
            raise ValueError('Baseline mismatch: ' + name)
        tables[name] = Dbc(raw, fields)
    models = tables['CreatureModelData.dbc']
    displays = tables['CreatureDisplayInfo.dbc']
    if len(models.rows) < 100 or len(displays.rows) < 1000:
        raise ValueError('Extraction fixtures are not full client DBC baselines')
    if any(row[0] == model_id for row in models.rows) or any(row[0] == display_id for row in displays.rows):
        raise ValueError('Provisional ID collision')
    row = [0] * 28
    row[0], row[2], row[4] = model_id, models.text(relative.as_posix().replace('/', '\\')), bits(1)
    # Pinned core doubles CollisionWidth: store the horizontal half-width.
    row[14], row[15] = bits(max(abs(low[1]), abs(high[1]))), bits(high[2])
    row[17:23] = [bits(value) for value in low + high]
    row[23], row[24] = bits(1), bits(1)
    models.append(row)
    row = [0] * 16
    row[0], row[1], row[4], row[5] = display_id, model_id, bits(1), 255
    displays.append(row)
    encoded = {name: table.encode() for name, table in tables.items()}
    report = {'scope': 'Offline display candidate only; not installed or live ID reservation',
              'model_id': model_id, 'display_id': display_id, 'model_path': relative.as_posix(),
              'model_sha256': hashlib.sha256(data).hexdigest(), 'bounds': {'low': low, 'high': high},
              'baseline_sha256': expected,
              'output_sha256': {name: hashlib.sha256(raw).hexdigest() for name, raw in encoded.items()}}
    output.mkdir(parents=True)
    for name, raw in encoded.items():
        (output / name).write_bytes(raw)
    (output / 'cart-display.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--model-path', required=True)
    parser.add_argument('--model-id', type=int, required=True)
    parser.add_argument('--display-id', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.source, args.baseline, args.client, args.model_path,
                           args.model_id, args.display_id, args.out), indent=2))


if __name__ == '__main__':
    main()
