# GRAV CODE 0.25.1 — teal

Package: `grav_code-0.25.1.fwsc`, identity `FM-1_73002501`. After 0.25.0 (`7e77214`: recording finished).

The screen's colour is teal now, not amber. The screen keeps its three neutral greys (rules, empty cells, labels);
only its two brightest steps carry the theme's colour: values, the knob being turned, the selected track, the step
bars and the piano roll's notes, the page's lights on screen. They are TEAL's, RGB 30 196 170 and 120 236 212, from
the palettes `device/gfx.c` offers (five added: VIOLET, TEAL, ICE, PINK, LIME, compared on the same screens before
choosing). The name on the splash screen takes the same teal. REC and an alarm stay red, the battery its green,
yellow and red. Nothing else changes; the installer and the site keep their own colours.

**The colours can be chosen.** GLO again opens a second page, SCREEN (GLO once more: DEBUG): K1 THEME picks one of
the ten palettes, TEAL (as it comes), AMBER (as before), CYAN, ICE, GREEN, LIME, VIOLET, PINK, RED or MONO. The
screen redraws in it at once; beside the knob, every palette's two colours and name, the one shown framed. It is
saved with the settings, in a byte reserved until now (older settings: TEAL). The splash at power-on keeps the teal:
it is drawn before the settings are read. It costs no RAM but that byte; the palettes were in the firmware already.

Every picture in the manual and the READMEs is retaken, the two GIFs too.

Validation on 26 October 2026: all 16 simulator scenarios pass (the new `theme.txt`: the knob's order, TEAL at its start, K2 and K3 doing nothing, SAVE and a restart, GLO's three pages); device build/compiler checks pass: image
**401780 B** (+736), RAM **91992 / 98304 B**, pool **332968 / 344064 B**, package **611496 B**.

Package SHA-256: `42f03973c39264cf242b9ebdf894138840e9ca1fae8524fd1bd1e824b1f3e9ec`.

Not installed on the FM-1 in this change: how the teal looks on the panel itself needs a look.
