# Terykons 0.21.0

Package: `terykons-0.21.0.fwsc`, identity `FM-1_73002100`.

- **Engine-specific knob labels:** WAVESHAPE shows FOLD / ASYMMETRY / CURVE, FM 2-OP INDEX / FEEDBACK / RATIO,
  six-op FM BRIGHTNESS / ENVELOPE / PATCH; each engine has its own three names. The analog bass drum is KICK on
  screen; its saved engine ID is unchanged.
- **ALGO selects the engine**, with its animated splash. **PRESET selects a sound inside that engine**: four
  factory starting points followed by matching user presets. Sounds apply immediately; editing a sound knob
  shows CUSTOM. The footer displays the preset name.
- **64 user slots:** hold SAVE for the grid, OCT-/+ selects one of four pages, a white key saves/replaces its
  slot. Names are USER 01–64. The key does not play a note, emit MIDI or edit/record a sequencer step, even if
  SAVE is released first. Occupied slots show S or D for their track kind.
- A synth preset stores its three engine knobs; a dedicated drum stores all six controls. Synth-track drums
  have a separate preset list from dedicated drums. Voice allocation, other tracks, patterns, tempo, ENV, LFO
  and FX are unaffected. A normal SAVE tap still saves track settings and the current pattern.
- Preset writes queue until the transport is stopped and no voice is active. PRESET QUEUED is still RAM;
  **wait for PRESET SAVED before switching off**. Failed writes remain dirty and retry on the next SAVE tap.
  Flash writing can interrupt an effect tail; it is not scheduled while a sequence or a held voice is playing.
- A finished self-enveloped drum now finishes its unused outer envelope as well. Previously it could sleep
  with a release still marked active, preventing idle flash work indefinitely.

The preset bank is a validated versioned 1296-byte object in store object 2's A/B sectors, DE000/DF000 (the old
unused BLE reservation). Platform records carry CRCs and commit headers. Old BLE content is ignored by size and
magic; it is replaced only by an explicit preset save. Settings stay 92 bytes, with the existing migrations;
calibration, legacy sequence objects and the 97000..D7000 pattern bank keep their addresses.

Validation on 26 October 2026:

- Reviewed main `58c2370` from the previous ENV fix through 0.20: sequencer, track/voice allocation, dedicated
  drums, patterns, pipelined Beads on CPU1 and icon rendering. Its eleven simulator scripts, sequencer timing,
  pattern-bank fault/wear tests and 960 concurrent DSP comparisons passed in an independent clean snapshot.
- 0.21: all twelve simulator scripts pass, including `presets.txt` (filtering, six drum knobs, slot 1/64,
  reboot, SAVE/key ordering, no MIDI/step edits, deferred writing and failed-write retry). Both screenshot
  scripts pass; regenerated manual images were visually checked.
- `run_presets_test.py`: all 64 slots, format rejection, engine/kind filtering and encoder traversal.
- `run_seq_test.py`: sequencer timing and pattern-bank interruption, failed writes, damage and wear.
- `run_dual_core_test.py`: all 120 factory engine/track-kind combinations produce finite nonzero audio;
  finished-drum idle saving regression; six-op ENV/LFO regression; overload fade; 960 bit-exact one/two-core
  comparisons with 1–4 synth voices, four dedicated drums and FX off/on.
- Device build: image **378468 B**, RAM **88184 / 98304 B** (10120 B free), pool **332328 / 344064 B**
  (11736 B free). Package **611496 B**. RAM-text, register access and compiler instruction checks pass.

Package SHA-256: `7e3ca9fd6531621d0410b9efabdcde14f3fe68cce7486af9717a43b6307bb722`.

Not installed or auditioned on the FM-1 in this change. Factory presets are starting points based on the engine
controls; musical balance and sound-page CPU peaks with eight tracks still need checking on hardware. Editable
names, SysEx preset exchange and live recording are later work.
