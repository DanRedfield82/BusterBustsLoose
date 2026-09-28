================================================================================
  TINY TOON ADVENTURES: BUSTER BUSTS LOOSE!  --  PC Port V1.0 - 28th Sept 26
================================================================================

  A custom PC port of the Super Nintendo / Super Famicom game. The console
  hardware (65816 CPU, PPU graphics, SPC700 sound) is reproduced in native code
  so the game runs directly on Windows -- no separate emulator needed -- with a
  set of modern extras: a true widescreen mode, upscaling filters and CRT
  effects, colour profiles, save states, fast forward, fullscreen, and an
  in-game menu (cheats, level select, music player and sound test).

  This is a personal, non-commercial project. It does NOT include the game
  itself -- you must supply your own ROM of a cartridge you own.

  >>> IMPORTANT: this requires a good dump of the US (USA) ROM. <<<
  The port is built and tuned specifically for the US release -- the
  widescreen mode, level select, cheats and other tweaks are matched to that
  exact version's internals. Other regions (European / Japanese), bad dumps,
  and hacked ROMs are NOT supported and are rejected on load (you'll be asked
  to pick a different file rather than the game misbehaving). The one you want
  is the verified good US dump, commonly named:

      Tiny Toon Adventures - Buster Busts Loose! (USA).sfc

  (1 MiB, CRC32 AB48D27A. Copies with a 512-byte copier header are accepted.)

--------------------------------------------------------------------------------
  WHY DID I PORT THIS TO PC?
--------------------------------------------------------------------------------

  After porting Buster's Hidden Treasure, the next one had to be Buster Busts
  Loose - I was a big fan of it on the Super Nintendo! Now it can be played
  on a modern PC in proper widescreen as well as ultrawide! (This mode took ALOT
  of effort to look and play right - some areas of the game still display in 
  4:3 due to the nature of the game).

--------------------------------------------------------------------------------
  GETTING STARTED
--------------------------------------------------------------------------------

  Keep these together in the same folder:

     BusterBustsLoose.exe           (the program -- you can rename it freely)
     SDL2.dll                       (required -- must sit next to the .exe)
     <your ROM file>                (the US "(USA)" version -- .sfc / .smc)

  Then just double-click the .exe.

  FINDING THE ROM (automatic):
     On launch the program looks for your ROM in this order --
       1. A ROM given on the command line (or dragged onto the .exe).
       2. The path it remembered last time (saved in "bbl.ini").
       3. Any ROM file sitting in the same folder as the .exe.
       4. If it still can't find one, a message tells you which ROM is needed
          and a file picker opens so you can browse to it. The path you choose
          is remembered for next time.

  WRONG VERSION?
     Every ROM is checked before it is used. If the file is the European
     (PAL) or Japanese version, a hack, or a bad dump, a message says exactly
     what it found (for example "It is the European (PAL) version") and lets
     you pick the right file instead.

  The easiest setup is simply to drop your ROM in the same folder as the .exe.


--------------------------------------------------------------------------------
  KEYBOARD CONTROLS  (in-game)
--------------------------------------------------------------------------------

     Move ................... Arrow Keys   (or I J K L)
     Jump ................... Z   or  Spacebar           (SNES B)
     Drop kick .............. A   or  Left Shift         (SNES Y)
     Drop kick .............. S                          (SNES X)
     SNES A ................. X
     Dash ................... Q   or  W                  (SNES L / R)
     Start / Pause .......... Enter

     Open the menu .......... Esc, Right Shift or Backspace  (see MENU below)

  Every key can be changed under CONTROLS in the menu.


--------------------------------------------------------------------------------
  GAMEPAD CONTROLS  (Xbox-style layout; DualShock / DualSense, Switch Pro and
                     most other USB / wireless pads work too)
--------------------------------------------------------------------------------

     Move ................... D-Pad  or  Left Stick
     Jump ................... A  (bottom button)          (SNES B)
     Drop kick .............. X  (left button)            (SNES Y)
     Drop kick .............. Y  (top button)             (SNES X)
     SNES A ................. B  (right button)
     Dash ................... LB  or  RB                  (SNES L / R)
     Start / Pause .......... Start
     Open the menu .......... Back / View / Share / Minus  (or the Guide button)
     Fast forward ........... Right trigger (hold)

  Buttons are mapped by position, so the layout is the same on every pad.
  A controller plugged in while the game is already running is picked up
  automatically, and every button can be changed under CONTROLS in the menu.

  The game itself only uses Select as a second Start button, so the port uses
  it to open the menu. You can turn that off under CONTROLS ("Select opens
  menu"); Select + Start together then opens the menu instead.


--------------------------------------------------------------------------------
  HOTKEYS  (work any time, during play)
--------------------------------------------------------------------------------

     Tab ...... Fast forward (hold)  -- can be switched off under CONTROLS
     F5 ....... Save state         (into the current slot)
     F9 ....... Load state         (from the current slot)
     F10 ...... Toggle widescreen  (wider view; the HUD moves to the edges)
     F11 ...... Toggle fullscreen
     F12 ...... Screenshot         (saved in a "screenshots" folder)


--------------------------------------------------------------------------------
  THE MENU
--------------------------------------------------------------------------------

  Press Esc (keyboard) or Back/Select (gamepad) to open the menu. The game
  pauses while it is open.

  Navigating the menu:
     Up / Down ............ Move between items    (D-Pad or left stick)
     Left / Right ......... Change a setting
     Page Up / Page Down .. Jump a page           (LB / RB on a pad)
     Enter, Space or Z .... Select / confirm      (A, the bottom button)
     Esc, Backspace or X .. Back out / close      (B, the right button)

  A line at the bottom of the menu explains whichever item is highlighted.
  Every change takes effect straight away and is remembered for next time.
  "(default: ...)" below is how each setting starts out.

  RESUME
     Close the menu and carry on playing.

  LOAD STATE
     Go back to the save state in the current slot (the same as F9). It
     shows when that slot was saved, or "EMPTY" if it has no save yet.

  SAVE STATE
     Save the game in the current slot (the same as F5). If that slot
     already holds a save, it asks before overwriting it.

  SLOT
     Slot 1 to 9: the slot LOAD STATE, SAVE STATE, F5 and F9 use.

  LEVEL SELECT
     Jump straight to any part of the game. Your current game is not kept
     when you do this, so make a save state first if you want to come back.
     (The first time you open it, it takes a moment to get ready.)

       Difficulty ......... Children, Normal or Challenge.  (default: Normal)
       Skip stage intro ... ON: go straight into the action. OFF: play the
                            stage's story intro first.      (default: ON)

     Then pick where to start:
       Stage 1: Acme Looniversity .. 1-1 Hallway, 1-2 Corridor, 1-3 Library,
                                     1-4 Classroom, 1-5 Cafeteria (Boss)
       Stage 2: Wild West .......... 2-1 Frontier Town, 2-2 Saloon,
                                     2-3 Train Roof, 2-4 Flat Cars,
                                     2-5 Locomotive (Boss)
       Stage 3: Scary Mansion ...... 3-1 Mansion Gate, 3-2 Bell Tower,
                                     3-3 Candle Hall, 3-4 Laboratory (Boss)
       Stage 4: Football ........... 4-1 Football Game
       Stage 5: Sky High ........... 5-1 Balloon Field, 5-2 Biplane Chase,
                                     5-3 Hot-Air Balloons, 5-4 Airship,
                                     5-5 Pinball (Boss), 5-6 Pinball 2,
                                     5-7 Cloud Temple
       Stage 6: Star Warners ....... 6-1 Space Station, 6-2 Asteroid Belt,
                                     6-3 Reactor, 6-4 Hard Course,
                                     6-5 Robot Factory, 6-6 Final Battle
       Bonus Games ................. Game Time Roulette (the wheel between
                                     stages), Squash, Junction, Maze, Bingo,
                                     Seesaw
       Extras ...................... Ending, Staff Roll

  CHEATS
     Switch each cheat ON or OFF on its own. Cheats stay on until you turn
     them off, even after you close the program.

       Infinite health ........ Hearts are never lost and stay full.
       Infinite lives ......... The lives counter never goes down.
       Infinite dash .......... The dash meter never runs out.
       Invincibility .......... Enemies pass straight through Buster.
                                (Switched off automatically in the football
                                game, see NOTES.)
       Moon jump .............. Hold jump to float upwards.
       Start with 99 lives .... New games and continues start with 99 lives.
       Five hearts ............ Start with five hearts (Children and Normal).
       Star bonus ............. Small stars are worth 10.
       Debug stage select ..... Starting a new game opens the game's own
                                hidden stage select.
       Passwords always work .. Passwords are accepted on every difficulty.
       Disable all cheats ..... Turn every cheat off at once.

  VIDEO
     Display
       Display mode ....... Windowed, Borderless (a fullscreen window) or
                            Fullscreen (exclusive). F11 switches between
                            windowed and borderless.     (default: Windowed)
       Monitor ............ Which display the game appears on, if you have
                            more than one.
       Window size ........ 1x to 8x the SNES picture. If the size you pick
                            doesn't fit your screen, the biggest one that
                            fits is used.                (default: 3x)
       V-Sync ............. Sync to your monitor's refresh rate. Turn it off
                            if you see stutter on a high refresh rate
                            display.                     (default: ON)

     Widescreen
       Widescreen ......... How wide the gameplay picture is:
                              Off (4:3) ... The original picture, no extra
                                            view at the sides.
                              16:10 ....... A little wider, for 16:10
                                            monitors and laptops.
                              16:9 ........ Standard widescreen TVs and
                                            monitors.
                              21:9 ........ Ultrawide monitors.
                              Fit window .. Matches the shape of your window
                                            or screen, whatever it is (up to
                                            about 22:9). Resize the window
                                            and the view follows.
                            F10 switches between Off and the last widescreen
                            mode you used.               (default: 16:9)
       Re-anchor HUD ...... ON: in widescreen the life/dash display moves to
                            the left edge of the screen and the lives/stars
                            display to the right edge. OFF: they stay where
                            they are in the 4:3 picture. (default: ON)

     Picture
       Aspect ratio ....... 4:3 (CRT): slightly wide pixels, the way the game
                            looked on a TV of the time.
                            Square pixels: sharper, but the picture is a bit
                            narrower.
                            Stretch: fill the whole window (distorts the
                            picture).                    (default: 4:3 (CRT))
       Upscale filter ..... Smooths the pixel art before it's scaled to the
                            window. None, Scale2x, Scale3x or Scale4x (keeps
                            it crisp), xBR 2x or xBR 4x (rounds off edges).
                                                         (default: None)
       Scaling ............ How the picture is fitted to the window:
                            Sharp (nearest pixel), Smooth (blurred),
                            Sharp bilinear (crisp, without uneven pixel
                            sizes), Integer (whole multiples only, with a
                            border around it).           (default: Sharp)
       Scanlines .......... Dark lines like a CRT television: Off, Light,
                            Medium or Heavy.             (default: Off)
       CRT mask ........... Aperture grille pattern, like a CRT screen up
                            close. Looks best at 1440p and above.
                                                         (default: OFF)
       Sprite limit ....... Original: the SNES limit on sprites per line, so
                            busy scenes flicker and lose sprites.
                            Unlimited: no flicker or dropout, recommended in
                            widescreen.                  (default: Unlimited)
       Show FPS ........... Show the frame rate in the corner.
                                                         (default: OFF)

  COLOURS
       Profile ............ Original: the game's colours as they are.
                            Vivid / Extra vivid: richer colour for modern
                            screens.
                            Natural (CRT) / Warm CRT: the look of a CRT
                            television (Warm is a little warmer).
                            Cool: a slightly cooler, bluer tint.
                            Soft: gentler, less saturated colour.
                                                         (default: Original)
       Brightness ......... Overall brightness, 50% to 150%.   (default: 100%)
       Contrast ........... Difference between dark and bright areas,
                            50% to 150%.                 (default: 100%)
       Saturation ......... Colour intensity, 0% (greyscale) to 200%.
                                                         (default: 100%)
       Gamma .............. Brightness of the mid tones, 50% to 150%.
                                                         (default: 100%)
       Reset colours ...... Back to the original colours.

  AUDIO
       Volume ............. 0% to 100%.                  (default: 80%)
       Sound output ....... Stereo or Mono.              (default: Stereo)
       Mute in menu ....... Silence the game while the menu is open.
                                                         (default: ON)

  MUSIC PLAYER
     Listen to the game's music and sound effects. Bars at the top show the
     8 sound channels and what's playing. (The first time you open it, it
     takes a moment to start.)
       Mode ............... Music: the soundtrack, 27 tracks -- the title
                            theme, each stage's music, the bosses, the
                            ending, the staff roll and more. Select a track
                            to play it.
                            Sound effects: a sound test. Choose an effect
                            with Left / Right and press Select to hear it.
                            There are also four short jingles.
       Stop ............... Stop playback.

  CONTROLS
       Select opens menu .. ON: the Select button (Back / View / Share /
                            Minus on a pad, Right Shift or Backspace on the
                            keyboard) opens the menu. The game only uses
                            Select as a second Start button, so nothing is
                            lost. OFF: Select goes to the game, and Select +
                            Start together opens the menu.   (default: ON)
       Fast forward ....... ON: holding Tab (or the right trigger) speeds
                            the game up. OFF: the button does nothing, so it
                            can't be pressed by accident.    (default: ON)
       Keyboard ........... Change any key. Each SNES button can have two
                            keys (Left / Right picks which one). Highlight
                            it, press Select, then press the new key (Esc
                            cancels, Delete clears it). The shortcut keys
                            (menu, fast forward, quick save, quick load,
                            screenshot, fullscreen, widescreen) are changed
                            here too. "Reset keyboard to defaults" puts
                            every key back.
       Gamepad ............ Shows which controller is connected. Change any
                            button the same way (Y on the pad clears one).
                            Also here: "Left stick moves" (use the stick
                            like the D-Pad, default ON), the shortcut
                            buttons, and "Reset gamepad to defaults".

  RESET GAME
     Restart from the power-on screens (asks you to confirm first).

  CREDITS
     Where to find me: YouTube, Reddit and GitHub.

  QUIT
     Exit the program (asks you to confirm first).


--------------------------------------------------------------------------------
  SAVE STATES
--------------------------------------------------------------------------------

  Save states capture your exact position at any moment (not just between
  levels). There are 9 slots.

     - Pick a slot with SLOT in the menu.
     - Save with F5, load with F9, or use LOAD STATE / SAVE STATE in the
       menu.

  Saves are stored in a "saves" folder next to the .exe (or in
  %APPDATA%\BusterBustsLoosePC if that folder can't be written to). Save
  states from older versions of the port still load; states saved by a newer
  version may not load in an older one.


--------------------------------------------------------------------------------
  NOTES
--------------------------------------------------------------------------------

  WIDESCREEN
     Widescreen shows more of the level on the left and right than the original
     4:3 view -- the game really draws the extra scenery, and enemies, items
     and moving platforms live across the whole picture, so they come in from
     the edges instead of appearing in the middle. At the start and end of a
     level the picture is shifted so the level still fills the screen, and the
     HUD moves out to the screen edges. Title screens, maps, cutscenes and a
     few single-screen rooms are drawn for exactly 4:3 and are shown with side
     bars. Toggle widescreen any time with F10, and pick the width under
     VIDEO > Widescreen: 16:10, 16:9 (the default), 21:9 for ultrawide
     monitors, or Fit window to match whatever shape your window or screen
     is.

  PAUSES WHEN YOU CLICK AWAY
     The game pauses while its window isn't the active one, and carries on
     when you click back into it.

  SETTINGS ARE REMEMBERED
     Your display, sound and control choices are saved (in "bbl.ini", next to
     the program) and restored on the next launch. Delete bbl.ini to go back to
     the defaults.

  LEVEL SELECT
     Later areas are reached by starting the stage and finishing the areas
     before them behind the loading screen, so their graphics and music load
     exactly as in normal play.

  CHEATS
     Invincibility is automatically off during the football game, where
     being tackled is part of a play and nothing on the field can hurt Buster
     anyway. Everywhere else it leaves the game's own events alone (2-1's
     jump rope starts as normal).
     With infinite lives, 99 lives or the star bonus on, lives stop at 99.

  RENAMING THE .EXE
     You can rename the program to anything you like. It still finds SDL2.dll,
     your ROM, your settings and your save states, because those are located
     next to the program, not by its name.


--------------------------------------------------------------------------------
  TROUBLESHOOTING
--------------------------------------------------------------------------------

  "SDL2.dll was not found"
     Make sure SDL2.dll is in the SAME folder as the .exe.

  It can't find the game / "isn't the right version"
     Put your ROM file in the same folder as the .exe, or pick it in the file
     browser that appears on launch. Only the US version is accepted (see
     the top of this file); the message tells you which version you have.

  The window opens but the taskbar/Explorer icon looks wrong
     Windows caches program icons. Copying the .exe to a new name, or
     refreshing the icon cache, updates it.

  Stutter or tearing on a high refresh rate display
     Try switching V-Sync on or off under VIDEO.

  No sound
     Make sure your system audio is working and not muted for the app, and
     check the volume under AUDIO.

--------------------------------------------------------------------------------
  CREDITS
--------------------------------------------------------------------------------

  Find me over at https://www.youtube.com/adhgaming , on reddit as u/Darhkwing
  https://github.com/Darhkfox

  Built on:
     LakeSnes (SNES emulation core) by angelo_wf and contributors -- MIT licence
     SDL2 -- zlib licence
     Cascadia Mono font by Microsoft -- SIL Open Font License 1.1
  (licence texts are in the "licenses" folder)

--------------------------------------------------------------------------------
  LEGAL
--------------------------------------------------------------------------------

  Tiny Toon Adventures: Buster Busts Loose!, its characters, artwork, and the
  game data are copyright their respective owners (Konami / Warner Bros.).
  This port contains none of that content -- it requires a ROM dumped from a
  cartridge you legally own, and is intended for personal use only. Do not
  distribute the ROM or a copy of this program that includes game data or
  copyrighted artwork.

================================================================================
