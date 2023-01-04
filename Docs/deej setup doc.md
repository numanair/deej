# Deej Setup

This document covers setup for Deej software for use with MIX5R Pro and other similar controllers

## Requirements

* Deej supports Windows and Linux. These steps primarily target Windows, but the general steps apply to both.
* These steps will work for any Return to Paradise 5-channel mixer, including Deej-only versions. They come pre-flashed, so ignore any references to Arduino sketches.
* Deej uses *serial* to connect to the mixer. Serial only supports one connection at a time, so any other software that tries to connect will interfere. MIDI connections are separate, so those may remain active simultaneously.

## Setup

1. First download the required files:

   1. Download `deej.exe` and `config.yaml` from the latest release: <https://github.com/omriharel/deej/releases/latest>
   2. Move both files to their own folder. Make sure this folder is located somewhere with normal permissions, such as your user folder. Do **not** place it in Program Files!
2. Configure Deej to connect to the mixer  
   1. To determine the COM port, first plug in the mixer. Open Windows
Device Manager and expand the “Ports” section. The mixer will show as “USB Serial Device”, “USB-SERIAL CH340” or similar. If you are unsure which device it is, unplug it and take note of which one disappears.
   2. Open the `config.yaml` file with your favorite text editor. I recommend Notepad++ or VScode. This is just a text file with special formatting. While editing, make sure this formatting (including spaces) is preserved.
   3. Set the `com_port:` line in `config.yaml` according to the port found in the previous step. Include the COM part of the port name (COM5 for example).
3. Launch `deej.exe`. Deej runs in the background with no GUI, so it may not look like it launched at first. Check the System Tray for the Deej icon.  
4. Configure your fader mappings in `config.yaml`. Deej will automatically reload changes when they are saved, so no need to relaunch it.
   1. Refer to the example mappings for the syntax. Note that the fifth fader (#4) is an example of multiple apps assigned to a single fader.
   2. The mapping names are normally the name of the executable ("example.exe"), but some games use a separate launcher process. To find the right name, either look in Task Manager, or use the instructions here: [Deej FAQ](https://github.com/omriharel/deej/blob/master/docs/faq/faq.md#how-can-i-find-the-exe-name-of-insert-app-here)
   3. Special Mappings: The special mappings are listed at the top of the default config. Apps can be excluded from `deej.unmapped` by adding them to an extra (non-existent) fader in the config. For example, the config would list faders 0 through 5 for a device with 5 physical faders.
5. Enabling Deej to run on startup automatically
   1. Find your user startup folder by pasting `shell:startup` into the run dialog (`win` + `r`).
   2. Place a *shortcut* to `deej.exe` in this folder.
