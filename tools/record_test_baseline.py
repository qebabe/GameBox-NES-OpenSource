#!/usr/bin/env python3
"""Record deterministic framebuffer/APU artifacts for a pinned test ROM."""

import argparse
import hashlib
import json
import zlib
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", help="filename from test-roms/manifest.json")
    parser.add_argument("--framebuffer", required=True,
                        help="raw RGB565 framebuffer capture used for CRC32")
    parser.add_argument("--pcm", required=True,
                        help="raw signed 16-bit little-endian PCM capture")
    parser.add_argument("--frame", type=int, required=True,
                        help="emulated frame number of the framebuffer capture")
    parser.add_argument("--sample-rate", type=int, default=44100)
    parser.add_argument("--note", default="")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    manifest_path = root / "test-roms" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entry = next((item for item in manifest["roms"] if item["file"] == args.rom), None)
    if entry is None:
        parser.error(f"ROM is not pinned in manifest: {args.rom}")

    framebuffer = Path(args.framebuffer).read_bytes()
    pcm = Path(args.pcm).read_bytes()
    if len(framebuffer) != 256 * 240 * 2:
        parser.error("framebuffer must be exactly 256x240 RGB565 (122880 bytes)")
    if len(pcm) == 0 or len(pcm) % 2:
        parser.error("PCM capture must contain signed 16-bit samples")

    entry["baseline"] = {
        "frame": args.frame,
        "framebuffer_crc32": f"{zlib.crc32(framebuffer) & 0xffffffff:08x}",
        "apu_pcm_sha256": hashlib.sha256(pcm).hexdigest(),
        "sample_rate": args.sample_rate,
        "pcm_samples": len(pcm) // 2,
        "note": args.note,
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
    print(f"Recorded baseline for {args.rom}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
