#!/usr/bin/env python3
"""Finish a macOS manual-import package without modifying signed binaries."""
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import xml.etree.ElementTree as ET

build, output, arch, deps = sys.argv[1:]
build, output, deps = Path(build), Path(output), Path(deps)
repo = Path(__file__).resolve().parent.parent
pair = subprocess.check_output(
    [str(repo / 'scripts/resolve-package-pair.sh'), str(build)], text=True
).splitlines()
archive, metadata = map(Path, pair)
root = ET.fromstring(metadata.read_bytes())
assert root.findtext('version').strip() == '0.4.3.1'
assert root.findtext('target').strip() == 'darwin-wx32'
assert root.findtext('target-arch').strip() == arch
assert root.findtext('target-version').strip() == '13.0'
output.mkdir(parents=True, exist_ok=True)
filename = f'xgrib_pi-0.4.3.1-darwin-wx32-{arch}-13.0-macos-{arch}.tar.gz'
root.find('tarball-url').text = (
    '\n    https://github.com/pob220/xgrib_pi/releases/download/'
    f'ventura-v0.4.3.1/{filename}\n  '
)
root.find('description').text += (
    ' Ventura compatibility test build: macOS 13.0 or later, with every bundled '
    'native dependency rebuilt from source for macOS 13.0. Requires testing '
    'on the recipient\'s Ventura system.'
)
xml = output / filename.replace('.tar.gz', '.xml')
ET.ElementTree(root).write(xml, encoding='utf-8', xml_declaration=True)

# Preserve the complete CPack layout and add the dependencies' license texts.
with tempfile.TemporaryDirectory() as temporary:
    payload = Path(temporary) / archive.name
    with tarfile.open(archive, 'r:gz') as source, tarfile.open(payload, 'w:gz') as dest:
        prefix = None
        for member in source:
            if prefix is None and '/OpenCPN.app/' in member.name:
                prefix = member.name.split('/OpenCPN.app/')[0]
            dest.addfile(member, source.extractfile(member) if member.isfile() else None)
        assert prefix
        license_root = f'{prefix}/OpenCPN.app/Contents/SharedSupport/plugins/xgrib_pi/bin/share/licenses'
        for license_file in sorted((deps / 'share').glob('*/copyright')):
            data = license_file.read_bytes()
            member = tarfile.TarInfo(f'{license_root}/{license_file.parent.name}/copyright')
            member.size, member.mode = len(data), 0o644
            dest.addfile(member, io.BytesIO(data))
    spec = importlib.util.spec_from_file_location('embed', repo / 'ci/embed-package-metadata.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.embed(payload, xml, output / filename)
print(output / filename)
