# Changelog

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
