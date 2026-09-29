"""Create an AMD64 import library from the pinned OpenCPN host and check a plugin."""

import argparse
from pathlib import Path
import re
import struct
import subprocess


def sections(path):
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError(f"Not a PE image: {path}")
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[offset:offset + 4] != b"PE\0\0":
        raise ValueError(f"Invalid PE signature: {path}")
    machine, count = struct.unpack_from("<HH", data, offset + 4)
    optional_size = struct.unpack_from("<H", data, offset + 20)[0]
    magic = struct.unpack_from("<H", data, offset + 24)[0]
    if machine != 0x8664 or magic != 0x20B:
        raise ValueError(f"Expected AMD64 PE32+: {path}")
    result = []
    for index in range(count):
        at = offset + 24 + optional_size + index * 40
        size, rva = struct.unpack_from("<II", data, at + 8)
        flags = struct.unpack_from("<I", data, at + 36)[0]
        result.append((rva, rva + size, bool(flags & 0x20000000)))
    return result


def exports(image, dumpbin):
    layout = sections(image)
    output = subprocess.check_output([dumpbin, "/exports", str(image)], text=True)
    result = {}
    for line in output.splitlines():
        match = re.match(r"\s*\d+\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)\s+(\S+)", line)
        if match:
            rva = int(match[1], 16)
            code = any(start <= rva < end and executable for start, end, executable in layout)
            result[match[2]] = "" if code else " DATA"
    if not result:
        raise RuntimeError(f"No named exports: {image}")
    return result


def build(args):
    names = exports(args.host, args.dumpbin)
    args.library.parent.mkdir(parents=True, exist_ok=True)
    definition = args.library.with_suffix(".def")
    definition.write_text("LIBRARY " + args.host.name + "\nEXPORTS\n" +
                          "\n".join(name + kind for name, kind in sorted(names.items())) + "\n")
    subprocess.run([str(args.lib), "/nologo", "/machine:x64",
                    "/def:" + str(definition), "/out:" + str(args.library)], check=True)
    print(f"Generated AMD64 import library from {len(names)} real host exports")


def verify(args):
    available = exports(args.host, args.dumpbin)
    sections(args.plugin)
    output = subprocess.check_output([args.dumpbin, "/imports", str(args.plugin)], text=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(output)
    active = False
    imported = []
    for line in output.splitlines():
        if line.strip().lower().endswith((".dll", ".exe")):
            active = line.strip().lower() == "opencpn.exe"
        elif active:
            match = re.match(r"\s*[0-9A-Fa-f]+\s+(\S+)", line)
            if match and match[1].startswith("?"):
                imported.append(match[1])
    if not imported:
        raise RuntimeError("Plugin has no OpenCPN host imports")
    missing = sorted(set(imported) - available.keys())
    if missing:
        raise RuntimeError(f"Plugin imports missing from pinned host: {missing}")
    print(f"Verified {len(imported)} OpenCPN imports against pinned AMD64 host")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("build", "verify"))
    parser.add_argument("host", type=Path)
    parser.add_argument("--dumpbin", required=True)
    parser.add_argument("--lib", type=Path)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--plugin", type=Path)
    parser.add_argument("--report", type=Path)
    arguments = parser.parse_args()
    if arguments.mode == "build":
        if not arguments.lib or not arguments.library:
            parser.error("build needs --lib and --library")
        build(arguments)
    else:
        if not arguments.plugin or not arguments.report:
            parser.error("verify needs --plugin and --report")
        verify(arguments)
