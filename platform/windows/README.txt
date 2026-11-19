wiimscreen for Windows
======================

wiimscreen is a remote for WiiM streamers (WiiM Mini, Pro, Pro Plus, Amp, Ultra):
now playing, covers, presets, favorites, Qobuz and Deezer search.
Project page: https://github.com/couzinc/wiimscreen

Requirements: Windows 10 (version 1903 or later) or Windows 11, 64-bit.
The computer and the WiiM must be on the same network.


Start
-----

1. Unzip the whole folder. Keep wiimscreen.exe, cacert.pem and DejaVuSans.ttf
   together.
2. Double-click wiimscreen.exe.
3. Windows can show "Windows protected your PC" (the program is not signed).
   Click "More info", then "Run anyway".
4. Windows Firewall can ask if wiimscreen may use the network.
   Allow "Private networks". wiimscreen needs this to find the WiiM.
5. wiimscreen finds the WiiM on the network. If it does not, start it with
   the address of the WiiM (the WiiM Home app shows it in the device settings):

       wiimscreen.exe --wiim 192.168.1.50

   To do this, make a shortcut to wiimscreen.exe, open its Properties and add
   " --wiim 192.168.1.50" at the end of "Target".


Options (add them to the shortcut's "Target", or type them in a terminal)
-------

  --fullscreen            Full screen (touch screens)
  --size 800x480          Size of the app picture (default: the largest of
                          1280x720 x 1.25, 1.5, 1.75 or 2 that fits the screen)
  --scale 0.75            Window size on the screen (default: the picture
                          pixel for pixel, smaller if it does not fit)
  --wiim 192.168.1.50     Use this WiiM
  --state <folder>        Settings and favorites folder
  --console               Show the messages in a console window
  F12 key                 Screenshot (in the "captures" folder of the settings)

Keys: arrow keys move the focus ring, Enter presses, Escape goes back,
F1 opens the settings menu, F3 opens the search, Space is play/pause.


Settings and log
----------------

Settings and favorites: %APPDATA%\wiimscreen
(type this in the address bar of File Explorer).

  settings.txt   the settings: one line per setting, the name, a tab, the value
                 (for example "lang<TAB>fr"). Close wiimscreen before you edit it.
  last.log       the messages of the last run. Send this file when you report a problem.
  previous.log   the messages of the run before.

To remove wiimscreen: delete this folder and %APPDATA%\wiimscreen.


What does not work on a computer
--------------------------------

- Microphone (voice search)
- Sound effects (taps and beeps)
- The setup page and the update page of the Tab5 (edit settings.txt;
  download a new zip to update)


Files
-----

  wiimscreen.exe   the program (no installation, no administrator rights)
  cacert.pem       root certificates for the secure connections to Qobuz, Deezer
                   and the cover servers (Mozilla list, as on the Tab5)
  LICENSE.txt      MIT License
