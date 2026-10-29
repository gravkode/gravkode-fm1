# Terykons 0.28.0 — HOME's terricons, the tracks' colours, neon engines

Package: `terykons-0.28.0.fwsc`, identity `FM-1_73002800`. After 0.27.2 (`7d5e934`: the code tidied).

![One bar playing, in the simulator: HOME's terricons at the left, step mode's eight tracks at the right](img/seq-playing.gif)

HOME shows the song as a ridge of eight terricons instead of 0.27's eight concentric rings. What they show and how
they move is unchanged; only the shape and the layout are new.

- **A terricon for each track.** S1 stands at the left and D4 at the right, all on one ground line, each in its
  track's colour (rose, orange, amber, yellow, green, teal, blue, violet, left to right). Each has its own height
  and place, S1 the tallest. Where two overlap, the lower one stands in front and hides the foot of the one behind.
- **The same states as the rings:**
  - a track with no step on has no terricon (its place stays empty);
  - a quiet one is its colour, dim and still;
  - a sounding one bends with its own sound: its level, measured before the mix and followed with an attack and a
    release, makes waves run along its slopes, with a glow under its line brighter the louder;
  - a muted one (or one silenced by another's solo) is grey and still.
- **Each engine moves in its own way:** a kick a strong, quick bulge, and its peak jumps up as it hits; a snare a
  sharper one; a hi-hat small, quick teeth; a bass big smooth waves; a pad slow ones. The waves leave the feet and
  the peak where they are.
- **The selected track's terricon** is thicker and white at its core, with a marker and its name over its peak. Its
  level, six segments in its colour, is now at the top right.
- **The rest of HOME is unchanged:** the bottom band (TRK, engine, MUTE / SOLO / NO VOICE / FREEZE, CPU, the sound's
  name), K1 TEMPO, K2 SWING, K3 LENGTH, about 30 frames a second while anything sounds (10 when the sound is busy).

**Step mode in the tracks' colours.** The terricons' colours are fixed, a colour a track, so step mode (SEQ) uses
them too: each row's name and its steps are in its track's colour, a step brighter the harder it plays. The selected
track's row stands on a ground of its colour, its steps at their brightest. A muted track's name stays red and its
steps grey, as before.

**The engines' icons redrawn as neon pictures** (after a poster of the 24 engines), each in its own colour:

| Engine | Its picture |
| --- | --- |
| ANALOG | an orange square and saw wave (DETUNE: a second one drifting by) |
| WAVESHAPE | a violet ribbon of folded waves (FOLD folds it further) |
| FM 2-OP | blue lens-shaped loops (INDEX bends them, RATIO multiplies them) |
| GRAIN | a teal cloud of grains along a wave |
| ADDITIVE | its harmonics as lines, orange to pink |
| WAVETABLE | purple sheets of waves in perspective, the row lit |
| CHORDS | stacked purple planes, one a note of its chord |
| SPEECH | a face of blue dots in profile, its jaw moving, the words' wave beside it |
| SWARM | an orange spiral of dots, turning |
| NOISE | a cloud of white noise (RESONANCE: a ring in it) |
| PARTICLE | magenta orbs on tilted orbits, sparking |
| STRING | a red-orange ribbon of strings swaying after each pluck |
| MODAL | a blue dome, its ripples spreading round it |
| KICK, SYN KICK | red rings jumping out round a glowing core; SYN KICK its falling sweep |
| SNARE, SYN SNARE | a blue starburst at each hit; SYN SNARE a ring in a burst of noise |
| HI-HAT, RING HAT | gold spikes of metal; RING HAT two rings beating |
| VA VCF | a green resonant low-pass curve, the light under it falling away |
| PHASE DIST | a magenta spiral, wound tighter by AMOUNT |
| 6-OP FM 1, 2, 3 | spheres of wire loops turning: blue, teal, purple |
| TERRAIN | green wire mountains, a light travelling over them |
| STRING MCH | a gold bundle of detuned waves |
| CHIPTUNE | blue pixels stepping up an arpeggio |

They still move with their knobs and notes, as since 0.20. Light is added rather than painted: where lines cross
they grow brighter, towards white, as neon does, and a big icon's lines glow round them. Small ones (the tracks'
list, the presets' browser) keep fewer details and no glow. The README's sheet of icons is now a poster of them at
their biggest (`tests/icon_sheet.txt` shoots each engine's splash).

**In the code:** `draw_terricons` replaces `draw_rings`. A terricon is a line of 45 points from foot to foot. The
ground under it is filled black first, so it hides what stands behind it; then its line is drawn with `thick_line`
(0.27.2's fast thick line, now open-ended). The rings' state is renamed after what it is: each track's level
(`ui.level`, `levels_update`), its colour and look (`TRACK_HUE`, `TRACK_LOOK`). The test hooks follow:
`terricons`, `terricons_sounding`, `terricons_heard` (before: `rings...`).

Validation on 28 October 2026:

- All 18 simulator scenarios pass, and the screenshot script. `browser.txt` and `playback.txt` check the terricons
  through the renamed hooks: none at first, one for each track with a step, sounding and still as they play, stop
  and are muted.
- The pictures are new: HOME (`screen-seq.png`), step mode, the lists and the presets' browser over it, every
  page with an icon, the splash, GLO's version, the presets' and sound pages' pictures, the engines' poster
  (`readme-icons.png`) and both GIFs (the playing bar, the knobs easing).
- The sequencer, pattern bank, preset and 1018 bit-exact dual-core tests pass.
- Device build/compiler checks pass: image **417556 B** (+7104; the icons +6800), RAM **92568 / 98304 B** (**5736 B free**), pool
  **332968 / 344064 B**. Package **611496 B**; no soft-double helpers, unsafe instruction patterns or direct register
  access introduced.

Package SHA-256: `d61932417043f94758211c606a3ee2111f43b6b54ebd24d336f1e54b3fa41948`.

Not yet installed on the FM-1.
