# Changelog

## [Unreleased]

### Fixed

- Positional head tracking now works in the tutorial without replacing the view with a magenta checkerboard.
- Lean collision checks cover corners and edges, and preserve room to lean away when the game's camera starts close to a surface.

### Changed

- Settings move to `VotV\Binaries\Win64\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - Reticle settings, and a key that toggled the reticle. Here that is `[Camera] MoveCrosshair=0`: the game's crosshair now always follows your aim.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. End, Page Up and the Ctrl+Shift+Y and Ctrl+Shift+G chords were fixed before; every key of every action can now be changed or removed in `[Hotkeys]`.
- The settings keep their meaning under the fleet's names: `[Network] Port` is `UdpPort`; `[Tracking] LocalSmoothing` and `RemoteSmoothing` are under `[Smoothing]`; `[Camera] CollisionEnabled`, `CollisionMargin`, `CollisionChannel` and `CollisionReleaseSmoothing` are under `[Position]`; `[Hotkeys] YawMode`, a virtual-key code, is `YawModeKey`, a key list that also holds the `Ctrl+Shift+H` chord. `[General] DisableInMultiplayer` and `[Camera] AimTraceChannel` keep their names.
- The tracking mode (`Page Up`) and the yaw mode (`Page Down`) are saved to `CameraUnlock.ini` the moment they change, and the game starts in them next time. Turning head tracking on or off with `End` still changes the current session only.
- `uninstall.cmd` leaves `CameraUnlock.ini` and `HeadTracking.ini` in place, so a reinstall keeps your settings. It used to delete `HeadTracking.ini`.

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- `EnableOnStartup`, whether head tracking is on when the game starts. It defaults to on, as the mod always started.
- `LightFollowsHead` and `LightMultiplier` for the flashlight. Their defaults, on and `1.5`, are what the flashlight has done since it started following the head.

### Removed

- `[Camera] MoveCrosshair`. The game's crosshair always moves onto the point your aim is on.

## [0.0.0] - 2026-09-22

### Added

- Added head tracking for Voices of the Void: head rotation and lean move the
  first person view while the mouse keeps pointing where you are aiming.
- Added crosshair compensation, on by default: the game's own crosshair moves
  onto the point the interaction ray stops on, so it marks what you will pick
  up or read once your head is turned or leaning. `MoveCrosshair=0` leaves it in
  the middle of the screen.
- Added gameplay gating, so tracking pauses in menus, the pause screen, screens
  you are typing on, while asleep or dead, and during loading.
- Added a solo-only gate, on by default. The game has no multiplayer mode, so it
  never fires today; it is what stands head tracking down if a second player
  ever appears in the session.
- Added a lean clamp that holds the view off walls, so leaning does not put the
  eye inside level geometry.
- Added field-of-view compensation, so zooming does not change how far a head
  movement moves the view.
- Added a game folder prompt to the installer. This game is a direct download
  rather than a store install, so there is no library for it to look up; it asks
  where you extracted it, and still takes the folder as an argument.
- Added a guard for the tutorial, which draws its frame through a camera path
  that will not take a view position the mod supplied: any lean at all, down to
  a millimetre, turned the whole 3D view into a magenta and black placeholder
  image while the HUD kept drawing over it. Leaning is held at zero on that map;
  head rotation works there as everywhere else, and leaning is untouched on
  every other map.
- Added centring of a windowed game on the monitor it opens on, once the game
  has finished placing its window. A fullscreen or borderless window, and one
  the game already centred, are left alone.
