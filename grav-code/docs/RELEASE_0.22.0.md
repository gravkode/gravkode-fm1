# GRAV CODE 0.22.0 — the sound editor

Package: `grav_code-0.22.0.fwsc`, identity `FM-1_73002200`. After 0.21 (`05fb85f`: ALGO, presets).

Plaits' three macro knobs combine several musical settings. This version adds independent controls on sound
pages while retaining AUTO for the original macro behavior, plus a full editor for all three six-op FM banks.

- **Sound pages:** HOME advances OSC → engine details → OUTPUT → SEQ; held HOME + SELECT jumps pages without
  selecting another track. Dedicated drums keep DRUM/SHAPE, followed by LEVEL/ACCENT/HOLD.
- **96 non-FM advanced controls**, including independent waveform/filter/excitation controls and alternative
  output controls. OUTPUT provides synth tuning, level and AUX mix. Context hints identify AUX/WORDS/CLOCK.
- **151 FM patch fields per bank:** all six operators' frequency/mode/detune, level, four envelope rates and
  levels, keyboard/velocity/modulation scaling and enable; algorithm, feedback, sync, transpose, internal LFO
  and pitch envelope. Fifty-seven sound pages including the main macros and OUTPUT. Operator edits affect held
  notes; changing PATCH restores that factory template. Operator numbering follows DX, reversing packed order.
- **Knobs in the Octatrack's manner:** small (a 14-pixel ring), grey and white, nine ticks over their travel lit up to
  the value, a pointer easing to it; the name above, the value under; only the knob being turned takes the theme's
  colour, for a moment. Page title/index and a compact engine icon above. The easing redraws one 80×84 knob tile
  per frame, at most 30 fps (10 under high audio load). The complete screen is not
  animated. Missing controls remain blank; discrete choices have labels. Saving/error status is visible.
- **Expanded persistence:** every new engine control, FM patch, drum setting and OUTPUT travels with the sound
  preset and current track settings. Common ENV/LFO/FX remain project settings. Sixty-four user slots, four factory
  starting points per engine, eight pending RAM slots; a full queue explicitly refuses the next distinct save.
- **WAVETABLE no longer clicks when chosen.** Built afresh, it started its pitch at A0 and its differentiator
  empty, and its first samples peaked about 30 times over its level (since 0.10): a click as it was chosen, or on
  its first note. A few silent blocks now bring it to where it runs, as on Plaits, where the engine runs from
  power-on. No other engine changes; a new check builds each of the 30 engines anew and finds no start click.
- **Plaits' changed files are each marked "GRAV CODE"** (vendor/README.md). With every advanced control at AUTO,
  all 30 engines render sample for sample as in 0.20.0 (checked against a build of `4f2db52`).

Expanded sounds use a 184-byte record in an indexed 32 KiB flash log at E5000..ED000. CRC headers commit last;
compaction copies live records before erasing. The final page of each sector stays erased for update-loader
compatibility; a dirty legacy guard page prevents allocation there until a sector erase. The 0.21 bank remains
intact at DE000/DF000 and imports missing slots before audio starts, resuming after interrupted migration.
Settings append eight control blocks to the compatible 92-byte prefix, 1404 bytes total. Pattern and calibration
addresses are unchanged. Pending preset writes commit one sound per idle frame and recheck stopped transport/inactive voices before the next; flash writing
can interrupt an effect tail. Normal project SAVE should still be used while stopped.

Validation on 26 October 2026:

- All **13 simulator scenarios** pass, plus both manual screenshot scripts. New checks cover page navigation,
  animation, all six operators through save/reboot, independent track settings, 0.21 settings migration, bank
  filtering, failed writes and refusing a ninth distinct pending sound and pausing the queued writes when a new note arrives.
- Preset tests: all 64 expanded slots, 120 factory engine/track-kind combinations, payload/header cuts, corrupt
  newest-copy fallback, interrupted compaction/erase, 2000 replacements with wear across all sectors, reserved
  loader pages, interrupted 64-slot legacy import and preserving newer sounds.
- DSP: all 96 non-FM extra controls have audio effects in applicable contexts; all 151 FM fields map to native
  patch bytes; operator levels, first edit on a held factory note, subsequent edits and restoring its factory
  patch affect live output. AUTO agrees with the stock path; combined minima/maxima remain finite.
- No engine starts with a click: each of the 30 built anew, its first two blocks against its level.
- **1008 bit-exact concurrent comparisons**: 960 stock plus 48 with advanced controls, four synth/four drum
  voices and FX off/on; existing six-op ENV/LFO and overload-fade regressions pass. Sequencer timing and pattern
  bank fault/wear tests pass.
- Device build/compiler checks pass: image **395108 B**, RAM **91512 / 98304 B** (**6792 B free**), pool
  **332968 / 344064 B** (**11096 B free**). Package **611496 B**; no soft-double helpers, unsafe instruction
  patterns or direct register access introduced.

Package SHA-256: `134c38c4d1193146516fd06e19e0f6ac6d7103a73bd408fde7c7aaa3ae857b8c`.

Not installed on the FM-1 in this change. Listening, physical encoder feel and real CPU/XRUN measurements during
editing with eight tracks and FX still require hardware validation. The GIF is a simulator capture.

Full reference: [SOUND_CONTROLS.md](SOUND_CONTROLS.md).
