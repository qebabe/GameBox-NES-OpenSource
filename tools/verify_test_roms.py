#!/usr/bin/env python3
import hashlib
import json
import re
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    rom_root = root / "test-roms"
    manifest = json.loads((rom_root / "manifest.json").read_text(encoding="utf-8"))
    errors = []
    for entry in manifest.get("roms", []):
        path = rom_root / entry["file"]
        if not path.is_file():
            errors.append(f"missing: {entry['file']}")
            continue
        data = path.read_bytes()
        if len(data) < 16 or data[:4] != b"NES\x1a":
            errors.append(f"invalid iNES header: {entry['file']}")
            continue
        mapper = (data[6] >> 4) | (data[7] & 0xF0)
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != entry["size"]:
            errors.append(f"size mismatch: {entry['file']}")
        if mapper != entry["mapper"]:
            errors.append(f"mapper mismatch: {entry['file']}")
        if digest != entry["sha256"]:
            errors.append(f"sha256 mismatch: {entry['file']}")
        baseline = entry.get("baseline")
        if baseline is not None:
            if not isinstance(baseline, dict):
                errors.append(f"invalid baseline object: {entry['file']}")
            else:
                frame_crc = baseline.get("framebuffer_crc32")
                pcm_hash = baseline.get("apu_pcm_sha256")
                if frame_crc is not None and not re.fullmatch(r"[0-9a-fA-F]{8}", frame_crc):
                    errors.append(f"invalid framebuffer CRC32: {entry['file']}")
                if pcm_hash is not None and not re.fullmatch(r"[0-9a-fA-F]{64}", pcm_hash):
                    errors.append(f"invalid APU PCM SHA-256: {entry['file']}")
                if (frame_crc is None) != (pcm_hash is None):
                    errors.append(f"incomplete frame/audio baseline pair: {entry['file']}")

    compatibility_path = rom_root / "compatibility.json"
    if compatibility_path.is_file():
        compatibility = json.loads(compatibility_path.read_text(encoding="utf-8"))
        known = {entry["file"] for entry in manifest.get("roms", [])}
        recorded = {entry.get("file") for entry in compatibility.get("roms", [])}
        missing = known - recorded
        extra = recorded - known
        if missing:
            errors.append("compatibility records missing: " + ", ".join(sorted(missing)))
        if extra:
            errors.append("unknown compatibility records: " + ", ".join(sorted(extra)))
    if errors:
        print("\n".join(errors))
        return 1
    print(f"Verified {len(manifest['roms'])} redistributable test ROMs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
