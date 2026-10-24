"""Verify the standalone Android module, including linker dependencies and ABI."""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

binary = pathlib.Path(sys.argv[1])
readelf = sys.argv[2]
payload = binary.read_bytes()
assert payload[:6] == b"\x7fELF\x02\x01", "Expected ELF64 little-endian"
assert int.from_bytes(payload[18:20], "little") == 183, "Expected AArch64"
assert len(payload) < 8 * 1024 * 1024, "Module exceeds the existing .install 8 MiB buffer"
dynamic = subprocess.check_output([readelf, "-d", str(binary)], text=True)
for forbidden in ("libssl.so", "libcrypto.so", "libc++_shared.so", "libcurl.so", "libpython"):
    assert forbidden not in dynamic, f"Unexpected external dependency: {forbidden}"
needed = re.findall(r"Shared library: \[([^\]]+)\]", dynamic)
assert set(needed) <= {"libc.so", "libm.so", "libdl.so", "libz.so"}, needed
symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(binary)], text=True)
assert "koto_module_register" in symbols, "Missing entry point"
programs = subprocess.check_output([readelf, "-l", "--wide", str(binary)], text=True)
loads = [line.split() for line in programs.splitlines() if line.lstrip().startswith("LOAD ")]
assert loads and all(int(line[-1], 16) >= 16384 for line in loads), "Expected 16 KiB LOAD alignment"
report = {
    "file": binary.name,
    "abi": "arm64-v8a",
    "android_min_api": 26,
    "module_abi": 3,
    "version": "0.1.2",
    "apk_update_required": False,
    "transport": "embedded OpenSSL, verified system CAs, HTTP/1.1",
    "google_authenticated_request_tested": False,
    "android_runtime_tested": False,
    "needed": needed,
    "source_sha256": {
        source.name: hashlib.sha256(source.read_bytes()).hexdigest()
        for source in sorted(binary.parent.parent.iterdir())
        if source.suffix in {".cpp", ".hpp", ".sh", ".map"}
    },
    "size": len(payload),
    "sha256": hashlib.sha256(payload).hexdigest(),
}
binary.with_suffix(".build.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
