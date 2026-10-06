# Buster Busts Loose! — PC port

A Windows port of *Tiny Toon Adventures: Buster Busts Loose!* (SNES, Konami 1993),
with widescreen, modern display options, gamepad support, cheats, a level select
and a music player.

The game itself runs from **your own copy of the original ROM**. The port contains no
game code or assets; it runs the ROM on a built-in SNES core (LakeSnes) extended for
widescreen, then adds game-specific patches and a PC frontend.

## Getting started

1. Put `BusterBustsLoose.exe` and `SDL2.dll` in a folder (nothing else needs installing).
2. Put the ROM next to them:
   `Tiny Toon Adventures - Buster Busts Loose! (USA).sfc`
   (1 MiB, CRC32 `AB48D27A`; headered copies are accepted too).
3. Run `BusterBustsLoose.exe`.

If the ROM isn't found next to the program, a message says which ROM is needed and a file
dialog asks for it; the location is remembered. You can also pass a ROM path on the command
line or drag a ROM onto the exe. Any other file is rejected with a message naming what it
is, read from the ROM's internal header: the European (PAL) or Japanese version, a modified
USA ROM (hack or bad dump), or not a SNES game at all.

Press **Esc** or **Select** (keyboard: Right Shift; gamepad: Back / View / Share / Minus)
to open the menu. The game only uses Select as a second Start button, so the port takes
it over; you can turn that off under Controls, and Select + Start then opens the menu instead.

## Features

**Widescreen**
- 16:10, 16:9, 21:9 or *Fit window*, used during gameplay (F10 toggles it).
- The HUD is re-anchored: life/dash move to the left edge and lives/stars to the right.
  Panels that span the centre, like the football scoreboard, stay centred.
- More of each level is actually drawn to the sides. The game's level streaming,
  its level-load pre-draw and its sprite culling are patched in memory, so scenery shows
  up in the extra space. Enemies, items and moving platforms are created and kept across
  the whole picture too, so they come in from the edges instead of appearing inside it.
- The level's graphics and collision are checked every frame, so parts of the level the
  game only redraws for the old 4:3 screen never show up stale at the sides.
- On stages that scroll to the right by themselves (the train, the asteroid belt) Buster
  still stops where the right edge of the 4:3 screen was: the levels are laid out for
  him waiting there while the train comes to him (running further ahead got him killed
  at 2-3's boxcar). Behind him he is only pushed off at the widescreen picture's edge.
  On 2-3's collapsing bridge the extra width is shown ahead of the train (nothing behind
  it there is laid out for being seen). On the airship Buster can use the whole picture.
- At the start and end of a level, where the original camera stops, the picture is
  shifted so the level still fills the whole widescreen view (distant backgrounds shift
  at their own parallax rate; foreground layers the game lines things up with, like the
  mansion's candles and the train's hills, move with the level). One-screen rooms such
  as pinball are centred with even side bars in the stage's backdrop colour.
- The game can show up to 256 sprites instead of 128, so the extra columns' enemies and
  scenery don't take sprite slots from the middle of the picture, and the emulated CPU
  gets a little more speed during widescreen play so busy scenes slow down no more than
  in 4:3.
- Title screens, maps, cutscenes and the game over screen stay 4:3 (pillarboxed): they
  are drawn for exactly 256 pixels, so there is nothing correct to show at the sides.

**Video**
- Windowed, borderless fullscreen or exclusive fullscreen (F11), monitor selection and
  window size. Rendering uses Direct3D 11 (falls back to other renderers if unavailable).
- Aspect ratio: 4:3 CRT (the original 8:7 pixel shape), square pixels, or stretch.
- Upscale filters: Scale2x, Scale3x, Scale4x, xBR 2x, xBR 4x.
- Scaling: sharp, smooth, sharp-bilinear (crisp at any size) or integer.
- CRT effects: scanlines (3 strengths) and an aperture-grille mask.
- Unlimited sprites (no flicker), V-Sync, FPS counter.

**Colours**
- Profiles: Original, Vivid, Extra vivid, Natural (CRT), Warm CRT, Cool, Soft.
- Brightness, contrast, saturation and gamma sliders.

**Audio**
- Volume, stereo/mono, mute while the menu is open.

**Music player & sound test**
- Plays the whole soundtrack (27 tracks) through the game's own sound driver,
  with a voice visualiser and a play timer.
- Sound test for all sound effects and jingles.

**Level select**
- Start any area of any stage (28 areas), on Children, Normal or Challenge difficulty,
  optionally skipping the stage intro. Later areas are reached by starting the stage and
  completing the areas before them behind the loading screen, so their graphics and
  music load exactly as in normal play.
- Bonus section: the between-stage "Game Time" roulette wheel and each bonus game on its
  own (Squash, Junction, Maze, Bingo, Seesaw).
- Extras: the ending and the staff roll.

**Cheats**
- Infinite health, infinite lives, infinite dash, invincibility, moon jump,
  start with 99 lives, five hearts, star bonus, debug stage select, passwords on every
  difficulty. Invincibility stops the three places the game registers a hit on Buster
  (`$8F:F976`, `$84:FEF3`, `$84:D841`) instead of holding his hit signal (`$B8`) set, which
  kept scripted events waiting for him to be clear of a hit from ever happening (2-1's
  jump rope, `$83:AEF8`; football's plays); it is off in football, where being tackled
  is part of a play. Moon jump only acts while Buster rises from a jump. With the lives
  and star cheats, lives stop at 99 (a 1-up at 99 showed 00).

**Other**
- Keyboard and gamepad (XInput, DualShock/DualSense, Switch Pro and anything else SDL
  supports), fully rebindable, hot-plugging.
- 9 save-state slots (F5 save / F9 load, or Load state / Save state on the main menu), fast forward (Tab / right trigger; can be
  switched off under Controls), screenshots (F12).
- Pauses when the window loses focus (optional).
- Version V1.0 (shown in the menu); the menu's Credits page has links to the author.

## Default controls

| SNES      | Keyboard            | Gamepad (by position) |
|-----------|---------------------|-----------------------|
| B (jump)  | Z / Space           | bottom face button    |
| Y (drop kick) | A / Left Shift  | left face button      |
| A         | X                   | right face button     |
| X (drop kick) | S               | top face button       |
| L / R (dash) | Q / W            | LB / RB               |
| Start     | Enter               | Start / Menu          |
| Select    | Right Shift         | Back / View (opens the menu, see above) |
| D-pad     | Arrow keys / IJKL   | D-pad / left stick    |

| Shortcut          | Keyboard | Gamepad            |
|-------------------|----------|--------------------|
| Menu              | Esc, Right Shift | Select (Back/View), Guide |
| Fast forward      | Tab      | Right trigger      |
| Quick save / load | F5 / F9  | (bindable)         |
| Screenshot        | F12      |                    |
| Fullscreen        | F11      |                    |
| Widescreen on/off | F10      |                    |

In menus: arrows / d-pad to move, Enter / Z / bottom button to confirm,
Esc / X / right button to go back.

## Files

Settings are stored in `bbl.ini`. Save states go in `saves\`, screenshots in
`screenshots\`. These live next to the exe, or in `%APPDATA%\BusterBustsLoosePC` if
that folder isn't writable. Delete `bbl.ini` to restore defaults.

## Building from source

Requirements: Visual Studio 2022 (C compiler) and CMake 3.16+. SDL2 2.32.10 comes with
the source (its headers and the x64 libraries, in `third_party/SDL2-2.32.10`); the full
SDL2 development package for Visual C++ can be dropped in there instead.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Or run `package.ps1`, which builds and copies everything into `dist\BusterBustsLoose`.

### Android

The same code builds for Android (`android/`), with SDL2 compiled from source. You need
the Android SDK with NDK 28.2 and CMake 3.31.6 (both from the SDK manager) and JDK 17–21
for Gradle.

```
powershell android\fetch_sdl.ps1
cd android
gradlew assembleDebug
```

The APK ends up in `android/app/build/outputs/apk/`. On a phone:
- The ROM is picked once with Android's file picker and copied into the app's storage.
- Settings and saves live in app storage too.
- The picture fills the screen (widescreen "Fit window").
- On-screen touch controls appear (Controls > Touch controls). A Bluetooth or USB
  controller works too, and hides them.
- Back opens the menu.

### Source layout

| Path | Contents |
|------|----------|
| `src/snes/` | SNES emulation core (LakeSnes, MIT), with widescreen rendering, per-line HUD remapping, playfield clipping, colour tables and debug hooks added |
| `src/bbl.c` | Game wrapper: ROM check, frame runner, widescreen ROM patches, routine calls |
| `src/hud.c` | Widescreen HUD re-anchoring |
| `src/ui.c`, `src/uifont.h` | Menu drawing and its anti-aliased font |
| `src/cheats.c` | Cheat patches |
| `src/music.c` | Music player / sound test (drives the game's sound routines on a second instance) |
| `src/levels.c` | Level select table |
| `src/filters.c`, `src/upscale.c` | Colour profiles and upscalers |
| `src/main.c`, `src/menu.c`, `src/finput.c`, `src/audio.c`, `src/ui.c`, `src/config.c`, `src/bgtask.c` | SDL2 frontend |
| `tools/probe.c` | Headless test and reverse-engineering tool (`bblprobe`) |
| `tools/dis65816.py` | 65816 disassembler used during development |
| `tools/genfont.py` | Bakes the menu font into `src/uifont.h` |
| `tools/makeicon.py`, `res/` | Makes the exe and window icon (`res/icon.ico`) from `icon.png` |

### Technical notes (USA ROM)

- Game logic runs in the NMI handler; the idle loop at `$80:80C1` only performs sound
  uploads (`$80:9AA9`). The port calls game routines by stepping the CPU to that loop
  and injecting a `JSL`.
- Game state: `$30`/`$32` = mode/sub-mode (3/9 = stage intro, 3/10 = playing),
  `$68` stage, `$94` area (6 = ending, 7 = staff roll), `$6C` difficulty, `$8E` lives (BCD),
  `$9E` hearts, `$1D7E` debug stage select. Holding Select when the debug stage select is
  confirmed opens the bonus section; on stage 1 that is a hidden bonus-game menu
  (cursor `$0442`).
- The BG1 camera lives at `$1900` (`$1902` X, `$1925` level width in screens - 1).
- Level columns are streamed at camera +`$140` / −`$40` (`$80:C998`, `$80:C9E6`) and
  pre-drawn by a camera sweep on level load (`$80:C777`: +392 px, then back 8 px per step).
  Rows use `$80:CC87` (49 columns: `$80:CC8F`, and `$80:CCA0` for rows that start left of
  the level). In widescreen the column offsets follow the visible window every frame, rows
  cover the whole 64-column ring, and the load sweep is widened to up to 640 px. The camera
  cannot scroll left into void screens, so the sweep never starts more than one screen past
  the level's right edge.
- BG1 and BG2 are 512 × 512 px tilemap rings; BG1's collision map (`$7F:7000`, read by
  `$80:EAAB`) mirrors its ring cell for cell. Every cell the column/row writers
  (`$80:CAF4` / `$80:CC87`) fill is recorded, and before each frame the streamed range and
  the visible picture are checked: cells still holding another part of the level (left
  behind by events that only redraw the original 256 px, like entering the lit café, or by
  switching widescreen on mid-level) are redrawn straight away from the level data (screen
  blocks and metatiles in `$7F`, tile collision at `$7F:CC00`), graphics and collision
  together. Cells the game drew itself are never touched (objects change some of them:
  platforms, animations): every word the game writes into a ring through the VRAM port
  (a PPU hook) marks that cell as holding what the game wants there. Rows shown on
  scanlines the layer is scrolled away from its camera in the middle of the frame are
  left alone (2-3's collapsing bridge is such a band, see below). After a camera jump
  without a redraw (the train's, 1024 px up) what the game left on screen stays as it
  is inside its own 4:3 view and to the right of it, where it streams on; left of the
  4:3 view it is redrawn from the level, as the original never shows it there. A state
  load learns the picture the same way, one frame after the load (the per-line scroll
  record is from before it until then); rows shown on lines scrolled on their own are
  taken as they are (1-4's boss kitchen keeps its conveyor chains in rows below the
  picture and shows them on such lines: learnt as unknown, they were redrawn from the
  level once the camera moved). The record is audited one column a frame: a cell recorded
  as holding its level cell but showing the level from 512 px away is stale and gets
  redrawn (2-5's locomotive boss showed the tender with a column of the level ahead, the
  picture having moved left over it; a state load could learn it wrong the same way).
- 2-5's tender (the last car) has no lower left in the level data: the original camera
  only comes down to it (camera y 768) with Buster on the footplate past the cab, camera
  x 4522 or more, when the tender (map columns 548-571) is off screen. The wider picture
  showed the sky through it. The ring repair draws it in (tenderFill in bbl.c): under the
  number plate a plain panel framed like the plate (columns 552-563, rows 112-115), left
  of both the tender's right side mirrored (columns 548-551, rows 104-115). Only the
  tiles: the collision ring keeps the level's (empty) values. It checks the tender's tiles
  around it first and applies only in stage 2 area 5 ($68 = 1, $94 = 4).
- 2-5's handcar chase: once Buster boards the railcar at camera 5120, the train controller
  ($8130, slot $10C0, state $24 at $83:E3C8) switches BG1 to the chase map ($81:901F, 4x2
  screens) and clears the camera's high bytes (5120,768 -> 0,256). Those are the same
  ring cells, so the locomotive's last picture stays and the chase map streams in as the
  camera moves. The original's ring holds the level up to 320 px ahead of the camera
  there. With the extra width shown behind Buster it only reached 272 px ahead, the rest
  holding the level from 512 px back, and the chase (shown 4:3, scene43) streamed 376 px
  ahead. So 13 columns of boiler slid past in mid-air. A hook on the switch ($83:E3CE,
  chaseSwitch) draws the columns up to 320 px ahead from the level first, leaving out
  rows the game draws itself (the rails, in their own palette), and the chase streams at
  the original offsets (applyStreamOriginal). After the switch it's pixel-identical to
  4:3.
- BG2 pans with the view shift at its own scroll rate, fitted to its map. A BG2 moving
  1:1 with BG1 is part of the level and pans with it: 6-3's giant ball is a one-screen
  BG2 map moved by its scroll.
- 2-3's collapsing bridge: after the camera jump, the bottom of BG1 (from line 153) is a
  band scrolled by the bridge's own position (`$7EC084`), not the camera: a conveyor in the
  512 px ring. `$83:E81F` lays the deck in the column 256 px ahead at every 32 px step,
  `$83:E8B0` builds each trestle column once it is within 304 px, and the collapse clears
  it behind the train. In widescreen the build distance follows the picture's right edge
  (+16 px, no more: the ring repeats every 512 px), and the ring repair keeps out of the
  band (it put track back into the gap and moved the bridge's collision about). For the
  last stretch the game writes whole columns again (BG1's `+$2E` bit 15 clear: rows below
  line 152 included) so the mountain is in place when the bridge stops; 264 px ahead of
  the camera the band shows none of that on a 4:3 screen, but the widescreen column
  comes round the ring into the wider band (rock hanging in the air left of the train),
  so there such a column fills only the rows above the band (a hook where the column
  writer takes its row count, `$80:CB05`), and the build distance is the original one:
  the bridge stops there and all the original built falls, a longer build left 96 px of
  bridge standing over the gap before the rock. Enemies keep the original spawn range
  while the bridge collapses (an early flying lizard took the sprite graphics of the
  falling timbers), from the camera's cut down to it on (the band that tells the
  collapse is seen only a few frames after the cut, and in between the wider range
  spawned the bandit early). The cars
  are level maps too: the game moves each car from BG1 onto BG2 by jumping both cameras
  256 px at once (BG1's map has no car there, BG2's has it), and BG2 scrolls down as the
  car falls. Only the 4:3 picture is laid out for that, so here BG1 is not drawn left of
  the 4:3 view (it showed the car it had just given up, floating over the gap) and BG2
  beside it only as far as the car reaching out of the 4:3 view goes (its next car fell
  with the whole layer off to the right, and popped back up at the next jump). The PPU
  has a per-layer clip for this. The train there is in fact one car in BG1's map: its
  controller (type `$8130`, state at `+$12`, cars to go at `+$3E`) hands each car on
  when BG2's x reaches `$11E`, BG1's camera going back 256 px so the same car of the map
  stands next in line on the right (`$83:DC33`; after the last, forward 256 px: no car
  follows). The widescreen picture showed the right of the train empty until that jump,
  the next car appearing all at once; while one is to come (states 10 and 11, two or
  more to go) the ring repair shows there, above the band, what the jump will bring: the
  map 256 px to the left where the level has nothing, tiles and collision. The car in
  BG1's map is short, too: only its first 24 columns (from map x 2304, a screen's edge),
  as the rest is never on a 4:3 screen before the car goes over to BG2, whose map holds
  the whole car (32 columns from its left edge, one row lower). Right of the 4:3 screen
  the missing columns are drawn from BG2's where that row of the map matches BG2's,
  solid like their mirror image at the car's front (the train ended short of the next
  car, and the car's rear appeared at the hand-off, coupling them all at once). The
  enemy riding the car (list type `$0122`, at its front) is spawned when the spawn
  column, 304 px right of the camera, gets to it, once each time the camera passes it:
  48 px into the widescreen picture's right part, and on the next car shown early only
  once the hand-off has taken the camera back; it is first drawn two frames later.
  Until the game draws it, a stand-in of its sprite pieces (their graphics are already
  loaded) is drawn where it will be, in the sprite table's extension (not while its
  slot is taken: then it would not come). On the frame a hand-off's jump is first
  seen, the ring repair leaves BG1 alone: the picture still shows where the camera was.
  While the next car comes, the controller holds BG1's camera at `$0800` (`$83:DC2C`,
  while BG2's x is below `$60`); the auto-scroll moves it on half a pixel a frame, and
  every other frame the objects have been moved 1 px back with it (by the camera of the
  layer Buster stands on, `$08BD` bit 7: BG2; `$80:C559`) before it is set back. At 4:3
  Buster stands on BG2's car then; right of the 4:3 screen he can stand on BG1's next
  car and was pushed backwards, so there he is moved on again.
  Before that, from the moment BG2's camera jumps onto the car it will drop (at its
  map's left edge; the hills are at y 1024) until the cut down to the bridge, the game
  draws that car into BG2 a column at a time: at 4:3 its own place stays off the left
  of the screen, and it comes in from the right through the ring's wrap. The widened
  picture showed it under the train (the ring repair drew it all at once, posts and
  rails under the boxcar), so then BG2 is not drawn left of the 4:3 screen and is not
  repaired. During the tilting part the screen is mode 2 below a mode 1 HUD, with BG3's
  map switched to the offset table, so the HUD remap works per scanline (mode and BG3
  map are recorded for each line).
- The train (2-3 to 2-5) is one map of 32x6 screens where only the train's row is wide:
  above and below it most screens are void, open sky over the desert. Buster drops in
  through them at the start, the camera rises into them when he climbs onto 2-3's boxcar
  and falls through them when he falls off. The game draws nothing for a void screen
  (only its collision, `$FF`), so the ring keeps what it held there (old train in the sky
  over 2-3's hills); here the ring repair draws them empty, and a column counts as part
  of the level when it has any screen at all (the level-load sweep still uses the strict
  extent: the game's camera cannot scroll over void screens). With only the rows in view
  counted, the picture was clipped at the void screens and slid sideways whenever the
  camera went a line above the roof.
- The game over screen runs inside the stage's mode (`$80` = 7, camera at 0,0); it gets the
  4:3 treatment, or the level's view shift and clip moved it sideways or hid it. (Losing the
  last life during 2-3's bridge tilt leaves the screen 30 px high in 4:3 too: the tilt's
  offset in `$1922` carries over, as on the SNES.)
- The camera is read as signed: 5-6's door shakes the screen by writing BG1's camera from a
  table (`$84:F711`, values 2, -1, 1, -1 at `$81:EBB3`), and -1 read as 65535 put the playfield
  clip 65000 px away (the picture went black every third frame). During such a shake the
  view is fitted to the camera from before it, so the one-screen room shakes as in 4:3
  instead of being held still.
- Stages that scroll BG1 on their own (the airship) get no view shift; this is decided by
  the majority of scanlines, as some rooms scroll a few lines separately (the boss
  kitchen's conveyor belts).
- Buster's x (`$0887`-`$0889`: fraction, pixel, page) is relative to the camera. The
  movement code (`$80:E6A2`) keeps it between 18 and 238 and `$80:CFA2` ends his life
  16 px outside the screen; this only matters where the camera doesn't follow him:
  auto-scrolling areas (`$19D4` bit 0, the train and the asteroid belt; bit 3 scrolls
  vertically) and the airship, where the game scrolls BG1 itself. In widescreen both
  checks use the picture edges there (CPU hooks), within the level's extent, except the
  right limit where the camera scrolls right (bit 0): those stretches are timed for
  Buster waiting at x 238 while the train brings things to him, and at the picture's
  edge he got to them first (in 2-3 he walked off the car roof into the gap before the
  tunnel's mountain had slid over it). So the limit stays, and the picture moves left by
  the extra columns instead: it ends where the 4:3 screen does, and so does he; the
  extra width shows the train behind. Off the original screen his x leaves 0-255, and
  three places move it with 8-bit maths on the pixel alone (the bounce off a wall at
  `$80:DA4C`, the snaps to the tile edge at `$80:E41F` and `$80:E5E6`): across a page
  the page stayed and he stood 256 px away, off the screen, dead (found with the limit
  lifted: bounced back from x 263 to 511 at 2-3's boxcar); hooks there carry the page.
  2-3's mail nets go round every 512 px (x = 400 - counter `$7EC092`, +4 a frame, held
  at 0 a while before some nets; their poles are one endless BG3 strip scrolled by the
  same counter, so BG3 moves with the picture there): the game ends a net just off the
  left of the 4:3 screen (x -112), starts it again at 400 and holds the strip, and at the
  first net the strip's copy stands at -112, all of which the moved picture showed (a
  bare pole, then one frozen during the hold, a net appearing on the left). So the left
  of it is drawn apart: after a net has gone round, its pole and net go on to the left
  at the net's speed, a ghost of them, from the frame of the restart on and at its own
  height (nets hang at different heights on the same poles: the new net's pieces are
  drawn at the old one's y, given the draw routine at `$80:9200`; the PPU draws BG3 left of game x -96 with a
  scroll of its own, and the net's pieces, written while it waits right of the screen,
  are moved there at the sprite range helper; the object being drawn is noted at
  `$80:91FA` (X: its slot), where its x and y are copied to `$08`/`$0A` for its pieces, and the net's type has bit 15 set
  while right of the screen); otherwise nothing is drawn there until the net comes
  back. Left of the 4:3 screen the net passes the passenger car, which the original
  never shows it next to; its pieces get priority 3 so it goes in front (`$80:9279`).
  There an object past the original range on the right and moving on right is
  removed by its own range check, as in the original (the wider range kept 2-3's bandit,
  who walked off the end of the carriage roof and on through the air, just off the 4:3
  screen in the original). 2-3's collapsing bridge shows its extra width ahead instead
  (see above), from the camera's jump down to it until the camera stops scrolling on
  the mountain, and Buster's limit is the picture's edge there: the view changes with a
  camera jump (a cut) at once instead of panning, and until the picture follows the jump
  (a frame later, two after lag frames) the per-line record shows where the camera was,
  which counts as on the camera (the collapse's car hand-offs, 256 px jumps, made the
  picture flick back to 4:3's place for a frame or two). The
  airship's blimp top is solid only up to screen x 288 (`$84:85B6`); that edge moves
  with the extra columns.
- Football (4-1): on a pass the camera follows the ball and Buster is held at x 238, the
  right of the 4:3 screen, until it reaches him (the pass depends on it). In widescreen
  the picture moves left while he runs out for it (`$0880` type `$0001`, past x 140), so
  that limit is its right edge too. BG3 (the scoreboard, the RUN/PASS box) and the
  windows stay where the game puts them on a 4:3 screen. Before, BG3's playfield lines and
  the scoreboard's dark backing moved with the view, and the backing slid off the
  scoreboard. The pass box (the game's own window at the left of the 4:3 screen, showing
  the ball's flight) is a window too, and the extra columns now count as outside every
  window there (ppu `windowFixed`). Taken from the edge column, its dark fill had covered
  the whole left margin. The pink marker on the scoreboard's BALL ON bar and the ball dot
  in the pass box are sprites drawn from the screen-position slots `$0460` and `$04C0`.
  Their sprite table entries are noted at the attribute write (`$80:9279`, from the WRAM
  port address into the table at `$01E0`) and kept out of the view shift through the
  PPU's per-sprite x hook (`sprFixedX`). Before, the dot moved out of the box.
- The sprite writer (`$80:9242`) rejects X outside −16..255; a helper at `$80:FF80`
  (free ROM space) extends the range in widescreen.
- Enemies and items come from per-screen lists (bank `$82`, table pointer at
  `$81:F704 + (stage × 8 + area) × 2`; entries: x, y, type, slot, parameter). They spawn when
  the camera's spawn column/row (+`$130` / −`$30`, `$8F:F0A6`) crosses them and remove
  themselves 64 px outside the original screen (`$83:82C7` and inline copies). In
  widescreen the removal range follows the picture, list entries entering it are spawned
  early into free slots, and an object alive only thanks to the wider range gives its
  slot back when the game's own spawner spawns into it (the spawners look at an entry's
  slot before its range test, `$8F:F3D9` and others, so the object is hidden for that
  test and only cleared if the entry really spawns). Parent objects spawn children straight
  into fixed slots (`$84:BC08`), expecting them empty: objects alive or spawned only
  thanks to the wider range, and their children, count as extra, and when another object
  spawns into an extra object's slot (a CPU write watch on the object area) the slot is
  cleared first and that object's earlier writes into it are applied again. The writer
  is the object X points at, or, while the object loop runs an object (`$82:C94B`), that
  object: some fill in a child's slot with X pointing at it (`$83:ACE4`). An object the
  wider range spawned early or kept alive stays marked until it is removed, even once
  back inside the original range (its timing is not the original's: 3-3's `$84:BC42`
  spawned into a `$0270` that was still there), and a slot such an object spawns into
  is cleared first too. Rewriting a slot's type with the type it already has is a
  parent refreshing its child (3-1's `$83:F02A`), not a new object, but only when the
  writer is the object that spawned what is there: 2-1's barrel piles all use the same
  slots, and a leftover barrel of the previous pile otherwise kept its "drawn" flag. Save states carry
  these marks after the emulator's own data (older states still load).
- 2-1's barrel piles draw their bottom row of barrels into BG1 at fixed tilemap addresses
  once in range (`$83:B2D2`), and clear 11 × 4 cells there when the pile goes out of range
  (`$83:B290`, addresses from `$81:C8A5`). At 4:3 those ring cells still hold the pile
  then, off screen. In widescreen the columns at one end can already hold the level
  512 px along (arriving in 2-1 from stage 1, the Town Hall's porch lost a 32 × 32
  block), and the ones at the other end can still be on screen. Only the columns that
  still hold the pile and are off screen are cleared: the routine's rows are narrowed to
  them (its start address and count), or it is skipped.
- Offset-per-tile (mode 2) scenes, 2-4's rising and falling logs and its carriage: the
  game fills vertical offsets for the 32 columns of the 256 px screen (`$81:FF30`, from a
  ring of 64 column heights at `$1D80`). The widescreen columns get theirs from the same
  ring (a frame later, as the game's table reaches VRAM), but only columns the game
  itself shows with offsets at some point: the ring repeats every 512 px, so beyond that
  (the carriage after the logs, whose right end the game only shows once the animator
  has stopped at camera x `$1970`) they get the ring's flat value.
- A level object at `$82:C8AF` swaps sprite graphics sets mid-level (2-1 twice, 2-4's
  magicians): it queues the next set once it is less than 64 px from the left of the
  screen and the previous one again at 192 px. At 4:3 nothing that uses either set is on
  screen then; the widescreen picture can show both. Its tests (`$82:C8C4`, `$82:C8EB`)
  are hooked: the next set comes up to the picture's extra width on the right sooner
  (an early-spawned magician showed garbage) unless an object behind the trigger was
  drawn from the second sprite table (where the swapped sets go, `$7000`-`$7FFF`; Buster
  and stars use the first) inside the 4:3 screen, which would lose its graphics where
  the original shows them (2-1's hay bandit). The previous set comes back later only
  while something past the trigger is still in the picture and nothing behind it is,
  and sooner (from 112 px) once something behind it is back on the 4:3 screen (walking
  back, the original showed it scrambled until 192, and for good at 2-1's hay bales,
  which stop the trigger at 191). An object whose set is swapped out is hidden outside
  the 4:3 screen, where the original would not show it either, instead of drawn
  scrambled: the sprite pass (`$80:90ED` calls `$80:91F8` for every object) skips it
  (2-1's hay bandit stays in the picture after the swap while the duel's scroll shows
  the extra width behind Buster).
- 2-1's lasso bandit (`$8104`, `$83:AC71`) is never spawned early: as soon as he runs he
  spawns the second bandit and the camera's auto-scroll controller (`$806C`, `$19D4`),
  and early that started the duel's scroll 56 px before the original.
- Sprites: the game builds a 128-entry table at `$01E0` (high bits at `$03E0` through the
  pointer at `$02`, set up at `$80:90C1`) and stops when that pointer wraps (`$80:92AC`);
  the NMI copies it to OAM at `$80:81BD`. In widescreen a full table is set aside and the
  game carries on into a second one, which the PPU draws after the first 128.
- Windows are in the game's screen coordinates: they move with the widescreen view shift
  (the dark café's flashlight), except on lines where a fixed BG3 overlay shows something
  (a text box's dark backing is a window, and the box stays put); the HUD's lines are
  the playfield's (2-4's tunnel darkness is a window: on the HUD's lines it began a
  view shift away from the tunnel's mouth). Camera jumps by whole 512 px rings (past the train's
  collapsing bridge the camera moves 1024 px up without a redraw) keep the ring cells as
  they are, as the game expects.
- The idle loop (`$80:80C1`) adds `$3A` to `$98` until the next NMI: that is the random
  number, so it depends on how much time each frame leaves (which differs in widescreen).
- Text boxes are BG3 too, framed with tiles `$2369`-`$2372`; rows with those tiles stay
  with the box even inside the HUD area (the end of 3-3's box starts just under DASH).
- The HUD is BG3, rows filled with the blank tile `$2300`; some stages split BG3 mid-frame
  for a scrolling foreground, so the remap is decided per scanline.
- Music table at `$82:FE69` (36 × 10 bytes); `$80:9A4D` requests an entry, and
  commands go to APU port 0 with an echo handshake.

## Credits and licences

- The port's own code is released under the MIT licence (`LICENSE`).
- Original game © 1993 Konami / Warner Bros. This project contains no ROM data. Tiny Toon
  Adventures and its characters are trademarks of Warner Bros.; this is an unofficial fan
  project, not affiliated with or endorsed by Warner Bros. or Konami.
- LakeSnes by angelo_wf and contributors (dinkc64 fork), MIT licence (`src/snes/LICENSE-LakeSnes.txt`).
- SDL2, zlib licence.
- stb_image_write by Sean Barrett, public domain.
- Cascadia Mono by Microsoft (menu text, baked into `src/uifont.h` by `tools/genfont.py`),
  SIL Open Font License 1.1 (`licenses/CascadiaMono.txt`).
- Cheat addresses are based on the published Pro Action Replay codes for the USA release.
