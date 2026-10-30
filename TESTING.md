# D3Hud — Testing Runbook

Replay-first principle: almost everything is developed against recordings and
synthetic scenes. In-game sessions are short, scripted, and rare.

## Layout

- `build/Release/D3Hud.Overlay.exe` — the overlay (all features below)
- `build/Release/d3hud_tests.exe` — unit tests (also via `ctest`)
- Recordings: any path; convention `recordings/<name>.bin`

## Build & automated checks

```
cd C:\Users\arne\Projects\D3Hud
cmake --build build --config Release
cd build && ctest -C Release
```

## M0 manual test cases (run on your desktop, no game involved)

Open a Notepad window first (most tests use it as the target).

| # | Command | What you should see | Pass criteria |
|---|---------|---------------------|---------------|
| T1 | `D3Hud.Overlay.exe --target Notepad` | Overlay appears over the Notepad window: circular minimap bottom-right, salvage panel right side, info box top-left of the overlay area | All three visible, positioned over Notepad, not on the whole desktop |
| T2 | (same, look closely) | Background around dots/panels is fully transparent | You can read Notepad's text THROUGH the overlay; no black box, no colored fringe around text |
| T3 | (default state) click on Notepad where the overlay covers it | Clicks pass through to Notepad | Caret moves / title bar reacts — Notepad gets every click |
| T4 | Press **F8**, then click the salvage panel rows / scroll the table | Overlay becomes interactive | Rows highlight on hover, scrollbar draggable; Notepad no longer gets those clicks. F8 again reverts to pass-through |
| T5 | Press **F9** | Panels (info + salvage) hide, minimap stays | Toggle works both ways |
| T6 | Watch ~30 s | Scene animates: player arrow travels a circle; white dots wander; blue dots move in 2 packs of 4; one yellow dot with 3 smaller yellow satellites; a purple dot sits still; 2 cyan diamonds + 1 green diamond are static. Dots leaving the circle become small rim ticks | Motion is smooth, no flicker, no ghosting |
| T7 | Look at info box | `~60 fps` (or your monitor refresh) | FPS ≥ 30 sustained |
| T8 | Move/resize the Notepad window | Overlay follows | New position/size matched within ~1 s, no stretching artifacts |
| T9 | `.\D3Hud.Overlay.exe --record demo.bin --seconds 5`, watch ~5 s, END; then `.\D3Hud.Overlay.exe --replay demo.bin` | During recording you see the live synthetic scene. On replay you see the **identical** 5 seconds repeating in a loop — watch the player arrow: it moves for ~5 s, then snaps back to where it started, forever. Console printed `recorded ~75 frames` | Info box shows `source: demo.bin`; loop point visible as the scene restarting |
| T10 | Press **END** | Clean exit | Console prints `exited cleanly`, process gone, F8/F9 no longer grabbed system-wide |

Note: `--colorkey` is a legacy fallback for machines where DirectComposition is
unavailable. It is known to render as an opaque black sheet on some systems —
ignore it unless DComp ever fails for you (T2 passing means DComp works).

Note: while the overlay runs, F8/F9/END are global hotkeys — they will trigger
even when other apps have focus.

## M2a — live player tracking check (~2 min, read-only)

1. In-game, in-world: **stand still for ~2 seconds** (the finder needs a
   settled position; a just-moved player's copies splinter and can lose to
   NPC crowds).
2. `.\D3Hud.Reader.exe --list-filter Diablo`, then
   `.\D3Hud.Reader.exe --attach <pid> --track-player`
3. Expect: `player @ (x=..., z=..., y=...)` within ~3 s, then `tracking`.
4. Walk around: lines like `[12.3s] (x= 438.21, z= 502.77, y= 0.00) moved 12.4`
   appear while you move, stop when you stop. Verify a few steps north moves
   one coordinate consistently.
5. If the finder picks an NPC crowd (position doesn't track you), stand still
   2 s and rerun — diagnostics print the top-5 clusters.

## M1a — first in-game capture session (~10 min, read-only)

This is the only in-game step for M1. The reader never writes to the game and
never sends input — it only reads memory. Use the throwaway account. Run the
game **borderless windowed** and get to a town first.

Commands (from `build\Release\`):

```
D3Hud.Reader.exe --list-filter Diablo      # find the game pid (Diablo III64.exe)
D3Hud.Reader.exe --attach <pid> --capture recordings\cap_town
```

Script:

1. Stand still in town → run `cap_town` capture; wait for `capture done`.
2. Walk around town ~30 s (mobs/NPCs around), then stand still → `cap_town_walk`.
3. Go somewhere with monsters (rift/bounty), fight briefly, stand still in a
   safe spot → `cap_combat`.
4. Optional but valuable: open your inventory with unided items on screen,
   screenshot it (ground truth), then → `cap_inventory`.
5. Jot one line per capture: zone name, what was nearby (packs? pylons? boss?).

Notes:

- Captures dump committed readable memory; expect ~1–3 GB per capture on disk.
- After this session, ALL offset development happens offline against these
  dumps — no further game time needed until the M2c verification session.

## Reporting failures

- Note the test number / capture name, what you saw, what you expected.
- If the overlay misbehaves visually, quit with END and re-run with a recording:
  `--record fail.bin --seconds 5` — send me the .bin plus description.
- If a capture fails partway, the `index.txt` shows how far it got.
