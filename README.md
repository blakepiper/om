# om

A full-terminal mantra screensaver: `ॐ मणि पद्मे हूँ` in flowing color.
Small C11 program, ANSI escape sequences, libc and libm. No curses, GPU,
interpreter, package manager, or external assets at runtime.

## Run

```sh
cd /home/przvl/om
./om
```

The launcher enters the flake's Nix development shell, compiles into `.cache/om`
when necessary, and runs it. Nix downloads the pinned toolchain into its store
on the first run; it does not install packages into your user or system profile.
Subsequent launches reuse the toolchain and binary. Internet access is needed
on the first run if the dependencies are not already cached.

Use a UTF-8 terminal with a Devanagari-capable font/fallback and Indic shaping.
The app sends complete words, including vowel marks and the `पद्मे` conjunct,
rather than breaking them into random codepoints. Font selection and shaping
are controlled by your terminal, outside the Nix shell. If the script is missing
or malformed, try `./om --ascii` to check the animation independently.

## Animation

- **River:** a relaxed downstream current curls around broad,
  slowly shifting eddies. Expanding ripples drift through the words while intersecting
  waves cast moving bands of light across the surface. Layered swells ripple along
  the top and bottom banks and blend into the deeper flow.
- **Bloom:** interwoven spiral strands breathe outward, curling over and under each
  other in a slowly tilting field. Depth shading and gentle perspective make the
  strands recede and return as waves travel along them.
- **Lotus:** counter-rotating petal rings form a folded, breathing sculpture.
  Neighboring layers curl at different angles, with slow tilting, perspective,
  and light moving over the folds.
- **Rain:** staggered falling mantra streams with soft traveling highlights.
  A slow shared breeze bends neighboring curtains as they descend.
- **Incense:** long smoke ribbons drift from right to left, curling through
  neighboring layers as gentle gusts stretch and braid the words. Light rolls
  along the folds; every word keeps traveling left.
- **Lanterns:** buoyant processions rise from bottom to top at different speeds,
  swaying sideways and glowing in loose clusters like floating prayer lanterns.
- **Mandala:** concentric prayer wheels turn in alternating directions. Eight
  luminous gates align and slip apart as the rings breathe, making a slowly
  changing geometric pattern across the screen.
- **Flag:** waves ripple through words across the whole screen, with moving
  highlights over the folds and no fixed edge or flag outline.
- **Lava:** soft bubbles of varied sizes swell, percolate upward, and dissolve
  near the top. Slow circulating words keep the whole screen active behind them.
- **Galaxy:** six dense spiral arms feed words inward toward a dark central black
  hole. The symbols whirl faster near the glowing core and disappear into it.
- Nine palettes: rainbow, jade, lotus, glacier, ember, nebula, saffron, peacock, prism.
- Each launch starts with a randomly chosen motion, curated palette, and animation
  phase. Explicit `--mode` and `--palette` choices stay pinned; `--seed N` repeats
  the same opening. Each palette transition takes
  the full eight-second interval, with no holds or abrupt color jumps. A broad,
  gentle color wave moves across the screen throughout the transitions.
  The automatic cycle uses the eight curated palettes; the full rainbow preset
  is opt-in. The cycle follows river → lotus → bloom → incense → lanterns →
  mandala → whirlpool → flag → lava → galaxy → rain → river, starting at a randomly chosen formation.
  Each morph takes 24 seconds, without dimming the scene or
  replacing its words. The same words keep their identities while their paths,
  positions, and brightness blend into the next formation. The interpolation
  preserves velocity at cycle boundaries, with no sudden stop or direction change.
  Use `--scene-interval 40` for longer, more gradual morphs, or `--mode river`
  to stay in one formation. Pressing `m` also morphs into the requested mode.
  Use `--mode rain` to stay in rain; `--mode aurora` is an alias for river.
- The animation fills the terminal, adapts to resizing, and restores the cursor,
  screen, and terminal settings on exit or suspension. The app shows only the
  animation: no startup hints, status text, or overlays. Keyboard shortcuts work
  silently. Space pauses the animation and automatic cycling.
  The animation uses your terminal's default background, including transparency.
  Faint words blend toward its configured color rather than toward black. The
  app queries that color invisibly at startup; terminals without color-query
  support use a fallback that keeps faint text colored.
- Motion defaults to 60 fps with continuous word trajectories and a stable frame
  schedule. A denser word field fills the screen, with a default density of 0.95;
  use `[` / `]` or `--density` to adjust it. Words keep their identity while moving;
  neighboring rows share their
  brightness to soften cell boundaries. Frames are composed before display and
  use synchronized terminal updates where supported to avoid clearing flicker.
- About once every two minutes, the small words gather into a large `ॐ मणि पद्मे हूँ`.
  Each gathering randomly chooses curling, waving, or scattered paths, taking
  4.5–7 seconds to assemble and another 4.5–7 seconds to dissolve. The complete
  block gently hovers, bounces around the screen like a DVD screensaver, or makes
  a horizontal ticker pass in either direction. Holds vary from 3–13 seconds,
  with longer holds for traveling text and shorter ones at small mantra intervals.
  Its size varies between gatherings and gently pulses while formed. The first
  gathering starts after 30 seconds and finishes assembling around 35–37 seconds.
  Each gathering word carries a small piece of the Devanagari strokes along its
  path, gradually condensing into that piece as it approaches. The pieces join
  into bright, filled letters and travel apart again during dissolution; there
  is no separate full-text overlay. This preserves the shaped strokes and vowel
  marks. The assembled text keeps its font proportions as it moves and scales,
  with margins on every side.
  Ticker passes and very wide terminals use one line only when it is tall enough
  to remain readable; otherwise they use two lines.
  Terminals smaller than 64 columns or 20 rows keep the ambient animation.
- While the large mantra forms and travels, half the words smoothly settle into
  straight falling columns around it, with bright heads and fading trails inspired
  by cmatrix. They retain the current palette. The background gently fades near
  the moving text to keep its outline readable, then flows back into the ambient
  formation as the mantra dissolves. The same words keep moving throughout;
  both position and velocity match at the transition boundaries.
- In every ambient formation, some words grow and shrink independently, with
  different pulse rates and smoothly randomized peak sizes. Larger words use
  embedded shaped outlines drawn in terminal characters, reaching almost five
  rows tall; the terminal's font size itself stays unchanged. They return to
  normal size as they join the full mantra. Terminals smaller than 40 columns or
  12 rows use normal-sized words. `--seed N` reproduces the animation variations.

The Devanagari spelling follows [Namo Buddha's mantra reference](https://www.namobudha.org/mantras/).
The four written words stay intact; the visualization is an artistic treatment.

## Controls

| Key | Action |
| --- | --- |
| `q`, `Esc`, `Ctrl-C` | Quit |
| `Space` | Pause/resume |
| `p` | Next palette |
| `m` | Next motion |
| `a` | Toggle automatic palette and motion cycling |
| `+` / `-` | Faster / slower |
| `[` / `]` | Fewer / more words |

```sh
./om                              # continuously morph through the formations
./om --mode river                  # stay on the flowing river with ripples
./om --mode cycle                  # explicitly select the default behavior
./om --scene-interval 40           # stretch each seamless morph to 40 seconds
./om --mantra-interval 32          # preview the first large mantra around 13–15 seconds
./om --no-mantra                  # keep only the flowing formations
./om --mode bloom --palette nebula  # keep a gentle violet / magenta color wave
./om --palette rainbow             # opt into the full spatial rainbow
./om --mode rain                  # stay in falling streams; palettes still cycle
./om --mode incense               # curling ribbons flowing right to left
./om --mode lanterns              # glowing words rising from bottom to top
./om --mode mandala               # breathing, counter-rotating prayer wheels
./om --mode whirlpool             # multiple swirling whirlpools
./om --mode flag                  # full-screen fabric-like ripples
./om --mode lava                  # rising lava lamp bubbles over a moving field
./om --mode galaxy                # spiral arms feeding a central black hole
./om --mode lotus --palette prism # pin both the motion and palette
./om --fps 30 --speed 0.6          # a slower, lower-update-rate session
./om --color 256                  # for terminals without truecolor
./om --list                      # list palettes and motions
./om --help                      # all options
```

Truecolor is selected when `COLORTERM` advertises `truecolor` or `24bit`;
otherwise the renderer uses xterm's 256-color palette. Use `--color truecolor`
if your terminal supports it but does not advertise it.

## Development

```sh
nix develop
bash scripts/run --help
# Optional integration tests, using a local Python 3 (stdlib only):
python3 tests/test_terminal.py
cc -std=c11 -O2 tests/test_morph.c -lm -o .cache/test-morph
.cache/test-morph
cc -std=c11 -O2 tests/test_gather.c -lm -o .cache/test-gather
.cache/test-gather
cc -std=c11 -O2 tests/test_colors.c -lm -o .cache/test-colors
.cache/test-colors
```

You can also build the standalone package with `nix build` or run it with
`nix run`. The flake supports Linux and macOS on x86-64 and ARM64; Linux is
tested here. Updating `flake.lock` changes the toolchain pin. `.cache/` is
disposable, and deleting it forces recompilation.

The integration tests use real pseudo-terminals to check the launcher, script
output, both color depths, moving word positions, complete frames, motion controls,
tiny and large resizes, duration,
signals, suspension, and restoration of terminal settings. They do not verify
the host terminal's font rendering.
The C trajectory checks simulate twenty-two complete morphs, checking position and
velocity continuity, uninterrupted brightness, visible words throughout, and
repeated destination changes during an active morph.
They also verify leftward incense motion, upward lantern motion, and sustained
screen coverage in each new formation.
The gathering checks cover two complete assemblies/releases at four terminal
sizes, verify solid lettering and surrounding words, and check for jumps while
the underlying formations continue to morph. Additional seeds exercise all three
hold behaviors and gathering paths, movement bounds, reproducible choices,
transition velocities, and independent word-size pulses.
They also check that falling words remain around every full-text behavior,
stay in vertical columns, and stop changing when the animation clock is paused.
Intermediate assembly and dissolution frames must place every stroke fragment
at a moving particle, preventing a stationary silhouette from appearing early.
Startup checks cover all motion and curated palette choices, seed reproducibility,
and preservation of pinned settings.

`src/mantra.h` contains about 14 KB of embedded monochrome outlines, generated
once by `scripts/generate-mantra.py`. Regenerating that optional asset requires
Pango, Cairo, and FreeSans Bold; building and running om require none of them.
The asset includes the full mantra and individual Devanagari and Latin words.

Possible follow-ups: configurable palettes and session presets. This version
keeps configuration in CLI flags to keep the implementation and dependencies small.
