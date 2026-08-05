# DIJI-NES test ROMs

The public repository does not redistribute ROM binaries. For local testing,
obtain compatible homebrew ROMs from their original project and verify that
their licenses permit your intended use:

https://github.com/retrobrews/nes-games

Use downloaded files for bring-up testing only. `manifest.json` records the
exact builds used by the maintainers, but the upstream license and distribution
terms remain authoritative.

Recommended first test order:

1. pong1k.nes - Mapper 0, very small.
2. flappyblock.nes - Mapper 0, simple input test.
3. invaders.nes - Mapper 0, simple game/audio test.
4. croom.nes - Mapper 0, CHR ROM path test.
5. tigerjenny.nes - Mapper 0, bigger game test.

The locally tested ROMs listed in `manifest.json` parsed as Mapper 0 or Mapper 1,
which is within the current DIJI-NES support range.

`manifest.json` pins the expected size, mapper and SHA-256 of every test ROM.
Run `python3 tools/verify_test_roms.py` before collecting compatibility,
framebuffer CRC or audio hash results so baselines cannot silently refer to a
different ROM build. `compatibility.json` keeps one explicit record per pinned
ROM; records remain `pending-hardware` until a real device run is reported.

After capturing one raw `256x240` RGB565 framebuffer and signed 16-bit
little-endian PCM window, record a deterministic baseline with:

```bash
python3 tools/record_test_baseline.py pong1k.nes \
  --framebuffer captures/pong1k-frame600.rgb565 \
  --pcm captures/pong1k-frame600.pcm --frame 600
python3 tools/verify_test_roms.py
```

The recorder stores framebuffer CRC32, APU PCM SHA-256, frame number, sample
rate and sample count. Baselines are intentionally not invented before the
first verified hardware capture.
