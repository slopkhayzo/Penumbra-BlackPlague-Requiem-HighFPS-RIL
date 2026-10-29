PenumbraBlackPlague.FPSFix
==========================

High-frame-rate fix for Penumbra: Black Plague and Penumbra: Requiem
(Windows, GOG and Steam builds). The games normally render at 60 FPS because
rendering is tied to the 60 Hz game logic. This plugin lets them render at
your display's refresh rate while logic and physics stay at their original
60 Hz, and smooths the camera, hands, objects, characters, particles, and
mouse look between logic steps.

One installation covers both games: Penumbra.exe (Black Plague) and
Requiem.exe live in the same folder, and the plugin detects which one is
running.

This is a release candidate. Please report problems.


Install
-------
1. Open the game's "redist" folder, the one that contains Penumbra.exe and
   Requiem.exe. For GOG this is usually
   ...\GOG Games\Penumbra - Black Plague\redist
   and for Steam (Black Plague and Requiem share it)
   ...\steamapps\common\Penumbra Black Plague\redist
2. Copy into it:
     PenumbraBlackPlague.FPSFix.asi
     PenumbraBlackPlague.FPSFix.ini
   and, if you downloaded the package that includes the loader:
     winmm.dll
3. Start the game as usual.

The plugin needs an ASI loader. The "with-Ultimate-ASI-Loader" package
includes one as winmm.dll. If the folder already has a winmm.dll, do not
overwrite it: you already have a loader, so use the "plugin-only" files.

Turn on VSync in the game's options, or cap the frame rate with your graphics
driver. The plugin has no limiter of its own, and menus can otherwise run at
thousands of frames per second.


Check that it works
-------------------
After starting a game, PenumbraBlackPlague.FPSFix.log appears beside the
plugin. Its first lines should include

    Adapter selected: Penumbra: Black Plague      (or Penumbra: Requiem)
    Hook transaction committed: ...

If it says "failed closed" instead, the plugin did not recognize your
executable and has left the game completely unmodified. That is expected for
builds other than the GOG and Steam ones.


Settings
--------
Edit PenumbraBlackPlague.FPSFix.ini. Every feature has its own switch under
[Features]. To change a setting for one game only, put the same key under
[BlackPlague] or [Requiem].

  Enabled = false (under [General])   turns the plugin off entirely
  MouseAxisCorrection = false         restores the stock mouse feel, which
                                      turns faster horizontally than
                                      vertically on widescreen displays
  MotionBlurTiming = true             only relevant if you enable motion blur
                                      in the game; not yet tested


What has been tested
--------------------
Black Plague: camera, mouse look, hands and held items, moving physics
objects, picking items up, an animated enemy, ragdolls, particles, image-trail
effects, the interaction cursor, and the main-menu, inventory, notebook and
death-screen cursors.

Requiem: camera, mouse look, moving physics objects, physics-driven
skeletons, particles, the interaction cursor, and the main-menu and inventory
cursors.

Not tested yet: motion blur timing (off by default), the numerical keypad
cursor, image-trail effects in Requiem (off by default there), and a complete
playthrough of either game.

Known limits: the in-game computer screens keep their original 60 Hz cursor,
and mouse look is not smoothed while one is open. Beam-type particle effects
are left at 60 Hz.


Reporting a problem
-------------------
1. Set Enabled = true under [Telemetry] in the INI and reproduce the problem.
2. Send PenumbraBlackPlague.FPSFix.log, and say which game and where.
3. If the game crashed, also send the newest file from
   %LOCALAPPDATA%\CrashDumps if one exists.
To see whether a visual glitch is the plugin's, set Enabled = false under
[General] and check whether it still happens.


Uninstall
---------
Delete PenumbraBlackPlague.FPSFix.asi, PenumbraBlackPlague.FPSFix.ini and
PenumbraBlackPlague.FPSFix.log from the redist folder.

winmm.dll is the ASI loader. Delete it too only if you installed it from this
package and no other mod in that folder needs it.


Credits and licenses
--------------------
MinHook by Tsuda Kageyu (see MINHOOK-LICENSE.txt).
Ultimate ASI Loader by ThirteenAG (see THIRD-PARTY-NOTICES.txt; loader
package only).


Misc
-------
For more game fixes, visit https://slop-blog.enkhayzomachines.net/fixes :)
