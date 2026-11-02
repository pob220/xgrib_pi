#!/usr/bin/env python3
"""Reject incomplete, duplicate or wrongly labelled catalogue publication sets."""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def validate(root, manifest, version):
    expected = manifest['catalogue']
    found = {}
    for path in sorted(root.glob('*/package/xgrib_pi-*.xml')):
        xml = ET.parse(path).getroot()
        fields = {key: (xml.findtext(key) or '').strip() for key in
                  ['name', 'version', 'api-version', 'target', 'target-version', 'target-arch']}
        if (fields['name'], fields['version'], fields['api-version']) != ('xGRIB', version, '1.21'):
            raise ValueError(f'{path}: wrong plugin, release or catalogue API: {fields}')
        matches = [name for name, platform in expected.items()
                   if all(platform[key] in ('*', fields[key]) for key in
                          ['target', 'target-version', 'target-arch'])]
        if len(matches) != 1:
            raise ValueError(f'{path}: unexpected or ambiguous catalogue platform: {fields}')
        name = matches[0]
        if name in found:
            raise ValueError(f'{name}: duplicate package metadata')
        archives = list(path.parent.glob('xgrib_pi-*.tar.gz'))
        if len(archives) != 1 or archives[0].stat().st_size == 0:
            raise ValueError(f'{name}: expected one nonempty catalogue archive')
        found[name] = path
    missing = sorted(set(expected) - set(found))
    if missing:
        raise ValueError('Missing catalogue platforms: ' + ', '.join(missing))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact_root', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    manifest = json.loads((repo / 'ci/catalogue-platforms.json').read_text())
    cmake = (repo / 'CMakeLists.txt').read_text()
    version = '.'.join(re.search(r'set\(VERSION_' + part + r' "(\d+)"\)', cmake)[1]
                       for part in ['MAJOR', 'MINOR', 'PATCH', 'TWEAK'])
    try:
        found = validate(args.artifact_root, manifest, version)
    except (ValueError, ET.ParseError) as error:
        parser.exit(1, f'Publication preflight failed: {error}\n')
    print(f'Publication preflight passed: {len(found)} platforms, xGRIB {version}, API 1.21')


if __name__ == '__main__':
    main()
