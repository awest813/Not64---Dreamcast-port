# Dreamcast frontend checkpoint — 2026-10-04

The menu-first build is `build/dc/not64-ui.cdi`; launch it with
`build/dc/Play-Not64-UI.cmd`. It preserves the optimized Stadium 2 renderer
flags and defaults to frameskip 7 (2, 3, 3, 3). It starts games from their
normal boot; it does not automatically load the earlier battle checkpoint.
Private game dumps are local packaging inputs and must not be distributed.

## Controls and screens

- Browser: A opens a folder or launches a game; B returns to the parent
  folder or asks to exit; Up/Down or the stick moves; triggers page;
  Left/Right switches storage; Start rescans; Y opens settings; X opens controls.
- Sources: CD `/cd/roms` and `/cd`, SD `/sd/not64/roms`, PC `/pc/roms`.
  Unavailable sources and empty folders have distinct messages. Folder
  navigation retains the parent's selection. ROM headers and lengths are
  checked before launch. Names and help are clipped inside TV-safe margins.
- Settings: sound, cartridge/Pak auto-save and auto-load, frameskip,
  presentation aspect, console logging and SD logging. Unsupported VMU,
  manual cable and timing controls are omitted. Changes apply immediately.
  B saves settings; failure explicitly reports that changes last this session.
- Controls: A selects one of four ports; edit each N64 binding, analog source,
  pause shortcut and axis inversion. Defaults can be restored per port.
  Menu input always uses the standard raw controls. Jump Pack rumble remains
  unavailable. Mappings persist in the ASCII settings file and survive game
  controller initialization.
- Pause: default Start+A+B; Continue, Restart, Return to games, Save state,
  Load state, Settings and Controls. Left/Right or triggers select slots 0–9.
  Overwrite, restart and leaving ask for confirmation. Failed native save
  leaves the game paused; the player can fix SD or turn auto-save off to leave.
  Held inputs are consumed when changing screens and do not leak into gameplay.

Settings, states and complete cartridge save files are staged in temporary
files before replacement. Write, flush, close and replacement errors fail the
operation without deliberately deleting an older save. FAT replacement uses
a `.bak` fallback and refuses to overwrite an existing recovery copy. This
is not a guarantee against storage removal or power loss at arbitrary times.
State format remains NOT64ST v3. Save data is under `/sd/not64/saves`.

Interactive menu sessions have no default diagnostic instruction/frame
budget. Explicit diagnostic launches retain bounded test behavior. Pause
Restart reloads the current game; Return to games also works for disc/argv
launches. Launch/runtime errors in interactive sessions return to the browser.

## Verification at this stopping point

- Native GCC 15.1 / KOS 2.3 / LTO player builds and CDI packaging pass.
- `tools/dc/run_ui_host.ps1` passes the standalone frontend contracts:
  release gating, analog hysteresis, repeat, paging, layout bounds, three ROM
  formats, unavailable/empty sources, nested browse/back/launch, strict config
  parsing, mapping persistence, overwrite/leave cancellation, native-save
  failure behavior, and preserving an older file when replacement fails.
  Core audio/state/native-save services are stubbed in this frontend harness;
  it does not qualify the emulator core or actual SD hardware behavior.
- Flycast 2.6 boots the new disc, displays the menu correctly, and opens the
  Diagnostics folder through real Maple input. Serial evidence is in
  `build/dc/validation/ui-console-start.txt`. The first capture used an earlier
  UI build; the final idle-refresh adjustment still needs controller QA.
- The prior accuracy-filter build artifacts and launchers are preserved.
  New UI artifacts use separate directories. Previous speed measurements
  apply to the qualified players, not to this unqualified UI player.

## Remaining qualification

The user requested a stopping point before complete end-to-end qualification.
Do not describe this checkpoint as fully polished or game-speed qualified.
Finish real controller navigation of every screen, SD persistence/error cases,
PVR paused-frame/texture preservation after resume, and a repeated Stadium 2
speed/graphics comparison. Native display borrowing drains PVR and saves the
visible framebuffer without resetting mode/cache; that path still needs a
running-game check. The Docker ILP32 core regression suite could not run because
the installed Docker Desktop engine is unavailable. Legacy smoke expectations
were updated for the new layout and neutral input frames, but the full core
suite remains pending.

Build from DreamSDK with `bash build/dc/build_ui_player.sh` followed by
`bash build/dc/package_ui_player.sh`. Logs and a source/artifact hash manifest
are under `build/dc/validation/ui-*`. Pre-edit frontend sources are preserved
under `build/dc/validation/ui-before-source/`.
