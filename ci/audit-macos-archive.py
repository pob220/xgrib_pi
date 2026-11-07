#!/usr/bin/env python3
"""Read every packaged Mach-O minimum OS and verify the local load graph."""
import argparse
import collections
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import tarfile
import xml.etree.ElementTree as ET

def version(value):
    return (value >> 16, (value >> 8) & 255, value & 255)

def macho(data):
    magic = data[:4]
    if magic not in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xfe\xed\xfa\xce'):
        if magic in (b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf'):
            raise ValueError('Unexpected universal binary in architecture-specific package')
        return None
    endian = '<' if magic[:1] in (b'\xcf', b'\xce') else '>'
    is64 = magic in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf')
    cpu, subtype, kind, count, size, flags = struct.unpack_from(endian + '6I', data, 4)
    result = {'architecture': {0x1000007:'x86_64', 0x100000c:'arm64'}[cpu], 'loads': [], 'rpaths': []}
    offset = 32 if is64 else 28
    for _ in range(count):
        command, size = struct.unpack_from(endian + '2I', data, offset)
        assert size >= 8 and offset + size <= len(data)
        if command == 0x32:
            platform, minimum, sdk, tools = struct.unpack_from(endian + '4I', data, offset + 8)
            assert platform == 1, 'Not a macOS binary'
            result.update(minimum_macos=version(minimum), sdk=version(sdk))
        elif command == 0x24:
            minimum, sdk = struct.unpack_from(endian + '2I', data, offset + 8)
            result.update(minimum_macos=version(minimum), sdk=version(sdk))
        elif command in (0xc, 0x80000018, 0x8000001f, 0x80000023, 0x20):
            start = struct.unpack_from(endian + 'I', data, offset + 8)[0]
            result['loads'].append(data[offset + start:offset + size].split(b'\0')[0].decode())
        elif command == 0x8000001c:
            start = struct.unpack_from(endian + 'I', data, offset + 8)[0]
            result['rpaths'].append(data[offset + start:offset + size].split(b'\0')[0].decode())
        offset += size
    assert 'minimum_macos' in result, 'Missing minimum macOS load command'
    return result

def audit(path, arch, maximum):
    binaries, metadata = [], []
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            assert not member.name.startswith('/') and '..' not in PurePosixPath(member.name).parts
            if not member.isfile():
                continue
            if PurePosixPath(member.name).name == 'metadata.xml':
                metadata.append(archive.extractfile(member).read())
                continue
            data = archive.extractfile(member).read()
            binary = macho(data)
            if binary:
                binary['path'] = member.name
                assert binary['architecture'] == arch, member.name
                assert binary['minimum_macos'] <= maximum, (member.name, binary['minimum_macos'])
                binaries.append(binary)
    assert len(metadata) == 1, 'Must contain exactly one metadata.xml'
    xml = ET.fromstring(metadata[0])
    assert xml.findtext('target').strip() == 'darwin-wx32'
    assert xml.findtext('target-arch').strip() == arch
    assert tuple(map(int, xml.findtext('target-version').strip().split('.'))) <= maximum
    names = collections.Counter(PurePosixPath(binary['path']).name for binary in binaries)
    assert names['libxgrib_pi.dylib'] == 1 and names['environmental-grib'] == 1
    for binary in binaries:
        for rpath in binary['rpaths']:
            assert rpath.startswith(('@executable_path', '@loader_path', '/usr/lib/', '/System/Library/')), (binary['path'], rpath)
        for load in binary['loads']:
            if load.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if 'libwx_' in load and binary['path'].endswith('/libxgrib_pi.dylib'):
                assert load.startswith('@executable_path/../Frameworks/'), load
                continue
            assert load.startswith(('@executable_path/', '@loader_path/', '@rpath/')), load
            assert names[PurePosixPath(load).name] == 1, (binary['path'], load)
    return {
        'archive': path.name,
        'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'architecture': arch,
        'maximum_minimum_macos': '.'.join(map(str, maximum)),
        'binary_count': len(binaries),
        'metadata_embedded': True,
        'local_load_graph_verified': True,
        'ventura_runtime_test': 'not run; built and smoke-tested on macOS 15',
        'binaries': binaries,
    }

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('archive', type=Path)
    parser.add_argument('--arch', required=True, choices=['arm64', 'x86_64'])
    parser.add_argument('--maximum', default='13.0')
    args = parser.parse_args()
    maximum = tuple((list(map(int, args.maximum.split('.'))) + [0, 0])[:3])
    print(json.dumps(audit(args.archive, args.arch, maximum), indent=2))
