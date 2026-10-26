# GRAV CODE 0.25.1 — teal

Package: `grav_code-0.25.1.fwsc`, identity `FM-1_73002501`. After 0.25.0 (`7e77214`: recording finished).

The screen's colour is teal now, not amber. The screen keeps its three neutral greys (rules, empty cells, labels);
only its two brightest steps carry the theme's colour: values, the knob being turned, the selected track, the step
bars and the piano roll's notes, the page's lights on screen. They are TEAL's, RGB 30 196 170 and 120 236 212, from
the palettes `device/gfx.c` offers (five added: VIOLET, TEAL, ICE, PINK, LIME, compared on the same screens before
choosing). The name on the splash screen takes the same teal. REC and an alarm stay red, the battery its green,
yellow and red. Nothing else changes; the installer and the site keep their own colours.

Every picture in the manual and the READMEs is retaken, the two GIFs too.

Validation on 26 October 2026: all 15 simulator scenarios pass; device build/compiler checks pass: image
**401140 B** (+96), RAM **91992 / 98304 B**, pool **332968 / 344064 B**, package **611496 B**.

Package SHA-256: `026653da0470a857a0a9ae7312345c5e166a14027f45cf49c091d9f335cf79a1`.

Not installed on the FM-1 in this change: how the teal looks on the panel itself needs a look.
