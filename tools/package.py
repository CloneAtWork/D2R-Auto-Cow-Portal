#!/usr/bin/env python3
"""Create the release archives (deterministic: fixed timestamps, CRLF text files).

Usage: package.py <version> <dll> <toml> <readme.md> <out_dir>

Produces in <out_dir>:
  cow-portal-in-town-<v>-d2rmm.zip   plugins/cow-portal-in-town.dll + config/cow-portal-in-town.toml
                                      (layout D2RMM imports from its d2rloader folder)
  cow-portal-in-town-<v>.zip         README.md + the D2RMM zip + manual/plugins + manual/config
  SHA256SUMS.txt
"""
import hashlib
import io
import sys
import zipfile
from pathlib import Path

TIMESTAMP = (2026, 10, 29, 12, 0, 0)


def crlf(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def make_zip(entries) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in entries:
            info = zipfile.ZipInfo(name, TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    return buf.getvalue()


def main() -> int:
    if len(sys.argv) != 6:
        print(__doc__)
        return 2
    version, dll_path, toml_path, readme_path, out_dir = sys.argv[1:]
    dll = Path(dll_path).read_bytes()
    toml = crlf(Path(toml_path).read_bytes())
    readme = crlf(Path(readme_path).read_bytes().replace(b"{VERSION}", version.encode()))
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    d2rmm_name = f"cow-portal-in-town-{version}-d2rmm.zip"
    d2rmm = make_zip([("plugins/cow-portal-in-town.dll", dll), ("config/cow-portal-in-town.toml", toml)])
    full = make_zip([
        ("README.md", readme),
        (d2rmm_name, d2rmm),
        ("manual/plugins/cow-portal-in-town.dll", dll),
        ("manual/config/cow-portal-in-town.toml", toml),
    ])
    files = {d2rmm_name: d2rmm, f"cow-portal-in-town-{version}.zip": full}
    for name, data in files.items():
        (out / name).write_bytes(data)
    sums = [f"{hashlib.sha256(data).hexdigest()}  {name}" for name, data in files.items()]
    sums.append(f"{hashlib.sha256(dll).hexdigest()}  cow-portal-in-town.dll")
    (out / "SHA256SUMS.txt").write_bytes(crlf(("\n".join(sums) + "\n").encode()))
    for line in sums:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
