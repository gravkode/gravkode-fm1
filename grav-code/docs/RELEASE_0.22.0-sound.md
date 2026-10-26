# GRAV CODE 0.22.0-sound — sound editor preview

Package: `grav_code-0.22.0-sound.fwsc`, identity `FM-1_73002200`. Based on 0.21 commit `05fb85f`.

Plaits' three macro knobs combine several musical settings. This preview adds independent controls on sound
pages while retaining AUTO for the original macro behavior, plus a full editor for all three six-op FM banks.

- **Sound pages:** HOME advances OSC → engine details → OUTPUT → SEQ; held HOME + SELECT jumps pages without
  selecting another track. Dedicated drums keep DRUM/SHAPE, followed by LEVEL/ACCENT/HOLD.
- **96 non-FM advanced controls**, including independent waveform/filter/excitation controls and alternative
  output controls. OUTPUT provides synth tuning, level and AUX mix. Context hints identify AUX/WORDS/CLOCK.
- **151 FM patch fields per bank:** all six operators' frequency/mode/detune, level, four envelope rates and
  levels, keyboard/velocity/modulation scaling and enable; algorithm, feedback, sync, transpose, internal LFO
  and pitch envelope. Fifty-seven sound pages including the main macros and OUTPUT. Operator edits affect held
  notes; changing PATCH restores that factory template. Operator numbering follows DX, reversing packed order.
- **Circular controls:** readable centered values, page title/index and a compact engine icon. Eased animation
  redraws one 80×84 dial tile per frame, at most 30 fps (10 under high audio load). The complete screen is not
  animated. Missing controls remain blank; discrete choices have labels. Saving/error status is visible.
- **Expanded persistence:** every new engine control, FM patch, drum setting and OUTPUT travels with the sound
  preset and current track settings. Common ENV/LFO/FX remain project settings. Sixty-four user slots, four factory
  starting points per engine, eight pending RAM slots; a full queue explicitly refuses the next distinct save.

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
- **1008 bit-exact concurrent comparisons**: 960 stock plus 48 with advanced controls, four synth/four drum
  voices and FX off/on; existing six-op ENV/LFO and overload-fade regressions pass. Sequencer timing and pattern
  bank fault/wear tests pass.
- Device build/compiler checks pass: image **394276 B**, RAM **91512 / 98304 B** (**6792 B free**), pool
  **332968 / 344064 B** (**11096 B free**). Package **611496 B**; no soft-double helpers, unsafe instruction
  patterns or direct register access introduced.

Package SHA-256: `fbba1ff6b4af705464ae5b8cc076f5d49ad0bf1b0d39c94854a5d6b0ff1aee25`.

Not installed on the FM-1 in this change. Listening, physical encoder feel and real CPU/XRUN measurements during
editing with eight tracks and FX still require hardware validation. The GIF is a simulator capture.

Full reference: [SOUND_CONTROLS.md](SOUND_CONTROLS.md).
