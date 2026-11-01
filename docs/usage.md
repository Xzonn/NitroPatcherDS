# NitroPatcherDS User Guide

NitroPatcherDS applies translation patches and other modifications to NDS game cartridges on a 3DS, 2DS, DSi, DSi XL (LL), or an NDS / NDS Lite with a SuperCard miniSD. Games read data from both the cartridge and the storage card, and save directly to the original cartridge.

## Installation

### 3DS / 2DS

You need a 3DS or 2DS with Luma3DS and FBI installed, and an NDS game cartridge compatible with your patch.

1. Extract `NitroPatcher3DS.zip` and copy `NitroPatcher3DS.cia` and the `nitro-patcher` folder to the root of your SD card.
2. Put your patch files in `nitro-patcher/patches/` on the SD card. The empty file named `Place patches here` marks this folder.
3. Put the SD card back in your console and install `NitroPatcher3DS.cia` using FBI.
4. Insert your game cartridge and open NitroPatcherDS from the HOME Menu.

### DSi / DSi XL (LL)

You need a DSi with Unlaunch installed, and an NDS game cartridge compatible with your patch. See [DSi Guide](https://dsi.cfw.guide/installing-unlaunch.html) for Unlaunch installation instructions.

1. Extract `NitroPatcherDSi.zip` and copy the `nitro-patcher` folder to the root of the console's SD card.
2. Put your patch files in `nitro-patcher/patches/`.
3. Put the SD card back in your console, insert the game cartridge, and turn on the console while holding A + B to open Unlaunch's Filemenu.
4. Select NitroPatcherDS, check that the bottom screen shows `sdmc:/nitro-patcher/app/NitroPatcherDSi.nds`, and press A to launch it.

To assign a startup shortcut, open Unlaunch's `OPTIONS`, assign the app to one of the `BUTTON A / B / X / Y` options, then choose `SAVE & EXIT`. Hold that button while turning on the console to open the app.

You can also select `nitro-patcher/app/NitroPatcherDSi.nds` in TWiLight Menu++. Set the app to run in DSi mode with RAM disk set to None. If the loader locks hardware access, the app automatically restarts through Unlaunch before showing the cartridge prompt. Keep the app and patches on the console's SD card, with the game cartridge in Slot-1.

### NDS / NDS Lite (SuperCard miniSD)

You need a DS-mode-compatible SuperCard miniSD running SuperFW, and a console with FlashMe configured to boot Slot-2 in DS mode.

1. Extract `NitroPatcherDS.zip` and copy `BOOT.NDS` and the `nitro-patcher` folder to the root of the SuperCard's storage card. Back up any existing `BOOT.NDS` first. `BOOT.NDS` contains the app, fonts and translations. Keep its name and location.
2. Put your patches in `nitro-patcher/patches/`.
3. Leave Slot-1 empty and use FlashMe to start the SuperCard in DS mode. SuperFW loads `BOOT.NDS`.
4. Select your language on first launch, then insert the matching retail NDS cartridge when the cartridge prompt appears and press A to read it. If no cartridge is found, check that it is inserted and press A again.
5. Select your patches and press START to prepare and launch the game. Keep both cartridges inserted while playing.

If Slot-1 already contained a cartridge at startup and pressing A does not identify it, remove and reinsert the game cartridge, then press A again.

Patches, settings and cache files are stored under `nitro-patcher` on the SuperCard's storage card. The app's `sd:/` paths refer to this card as well.

### Patch folder

Put patches directly in the `patches` folder. The app only lists patch files at this level.

## Supported patches

| Extension | Contents                                                |
| --------- | ------------------------------------------------------- |
| `.xzp`    | A NitroPatcher patch package                            |
| `.zip`    | A patch package with the same internal structure as XZP |
| `.xdelta` | An xdelta/VCDIFF patch made for a complete ROM          |

XZP packages can be used directly without extracting them. ZIP packages must follow the XZP structure. If a download contains a separate `.xdelta` file, extract that file and put it in the patch folder.

Check that your cartridge matches the game region and revision required by the patch.

### DSi-enhanced games on NDS / DS Lite

NDS and DS Lite can run the DS portion of DSi-enhanced games such as Pokémon Black, White, Black 2 and White 2. When preparing an XZP or ZIP patch, the app displays a DS mode notice and processes the DS files and overlays while preserving cartridge save access. DSi-exclusive games and DSiWare are unsupported.

An NDS cannot read the complete DSi region, so **Skip ROM MD5** is enabled by default for these games. You still need a patch for your cartridge's region and revision. If you disable this option, use a cache prepared with ROM MD5 verification on a 3DS or DSi.

Whole-ROM `.xdelta` patches and XZP or ZIP packages that include whole-ROM preprocessing for these games must be prepared on a 3DS or DSi:

1. On the 3DS or DSi, use the same cartridge, select the same patch files in the same order, and complete preparation.
2. Turn off the console and copy the matching cartridge and patch combination directory under `nitro-patcher/cache/` to the same location on the NDS storage card. To create a verified cache, disable **Skip ROM MD5** on the 3DS or DSi before preparing it.
3. On the NDS, select the same patches in the same order and press START to reuse the cache and launch in DS mode.

Caches prepared locally for the DS portion are stored separately from complete caches. To play in DSi mode on a 3DS or DSi, prepare a complete cache on that console.

## Selecting patches and starting a game

The first time you open the app, choose an interface language. Move up or down to preview each language, then press A to confirm.

At the cartridge prompt, insert your game cartridge and press A to read it. Once it has been read, the top screen shows the game icon and title. The bottom screen lists your patches.

1. Use Up and Down to move through the list. Press A to enable or disable a patch.
2. Press START to launch the game.
3. When you use a patch for the first time, the app prepares the required files and then starts the game.

The app remembers the enabled patches and their order for each game, and restores your selection the next time you open it. If no patches are enabled, pressing START launches the original game.

During preparation, the screen shows the current step, progress, elapsed time and estimated time remaining. While processing files, it also shows the current file number and total file count. The remaining time is estimated for the whole preparation process and adjusts to the actual processing speed.

Reopen the app after adding patches. To change cartridges or recover after removing and reinserting a cartridge, press X and select **Read another Game Card**. Follow the insertion prompt and press A. The app restores the saved patch selection for that game.

### Using multiple patches

Patches are applied in the order you select them. The numbers `[1]`, `[2]` and so on beside their names show this order. To change the order, deselect the patches and select them again in the order you need.

Follow the patch authors' instructions when combining patches. For example, if a modification requires a translated version of the game, select the translation patch first, followed by that modification.

You can combine multiple patches. Each patch uses the preceding result as its input. When ROM MD5 verification is enabled, its MD5 requirement is checked against that result; preprocessing also uses that result. An incompatible order may cause a preparation error. Follow the patch authors' instructions.

## Buttons and touch controls

| Control              | Function                                                                |
| -------------------- | ----------------------------------------------------------------------- |
| Up / Down            | Move the selection; scroll descriptions, error details or license texts |
| Left / Right / L / R | Change pages                                                            |
| A                    | Enable or disable a patch in the patch list; confirm a menu choice      |
| START                | Launch the game from the patch list                                     |
| X / SELECT           | Open the menu from the main screen                                      |
| Y                    | View information and a description for the highlighted patch            |
| B                    | Go back when indicated on screen                                        |

You can also tap list items and action labels on the bottom screen. If a patch filename is too long to fit, leave it highlighted to see the full name scroll across the screen.

Patch information comes from the package itself. The app indicates when a patch has no metadata or description.

### Returning from a game

During a patched game launched through NitroPatcherDS, hold L + R + Down + B for about two seconds to return to NitroPatcherDS. Save your progress in the game first.

On NDS / DS Lite, keep the flashcard inserted. After returning, follow the reinsertion prompt: remove and reinsert the game cartridge, then press A to read it.

On 3DS / DSi, this shortcut is unavailable when starting the original game without patches. On NDS / DS Lite, it also works without patches.

## Menu

- **Current game AP patches**: Enable or disable nds-bootstrap's built-in anti-piracy patches. Enabled by default and saved per game. Change this setting if the patch author asks you to disable built-in AP fixes.
- **Skip ROM MD5**: Skip the `md5.txt` match requirement when preparing XZP or ZIP packages. Saved per game and disabled by default, except for DSi-enhanced games on NDS / DS Lite. ZIP and xdelta integrity checks remain active. If whole-ROM preprocessing needs an MD5 to select its delta file, the app still calculates it.
- **Start original**: Launch the current cartridge without applying patches.
- **Read another Game Card**: Return to the cartridge prompt. Remove and reinsert your cartridge, then press A to read it.
- **Environment report**: Create a report about the console and SD card to help diagnose problems.
- **Interface language**: Multiple languages are supported, including Simplified Chinese, Traditional Chinese, Japanese, English and Korean. Press A to save your choice, or B to restore the previous language.
- **Repair memory card**: Check allocation chains, FAT copies and free-space information across the storage card. If the active FAT passes inspection and repairable differences exist, press A to back up filesystem metadata, synchronize FAT copies and update free-space information. Backups are stored under `nitro-patcher/repair/`; the screen shows the full path. Cross-linked, lost or invalid chains, or uncertainty about the correct FAT copy, require backup and repair on a computer. Keep the power on and leave the storage card inserted while repair writes are in progress.
- **About**: View the app version, author and open source projects used. Press A to read the licenses.

## Cache and save data

After preparation, the cache is stored in `nitro-patcher/cache/` on the SD card. It is reused when you use the same cartridge revision and patches with the same contents and order. Changing patch contents or the combination of patches creates a corresponding cache. Cached files can be reused indefinitely.

To free up space, turn off the console and delete `nitro-patcher/cache/`. The app will prepare the files again the next time you launch a patched game. Save data is stored on the cartridge, so deleting the patch cache does not delete your saves.

Your language, patch selections and per-game AP and ROM MD5 settings are stored in `nitro-patcher/config/settings.json`.

For patches containing `md5.txt`, disabling **Skip ROM MD5** causes caches prepared without verification to be rebuilt and checked. DSi-enhanced games on NDS require a complete, verified cache prepared on a 3DS or DSi when this option is disabled.

When an app update changes the preparation rules, an older cache is rebuilt automatically when that patch combination is used. Incomplete caches left by failed or interrupted preparation cannot launch games. Select the patches and start again to retry.

## Troubleshooting

For an unsupported format version, file name, directory layout or overlay configuration, open the error details and press B to return and change your patch selection. The corresponding controls on the touch screen also work.

If no patches appear, check that the files are directly inside `nitro-patcher/patches/` and have a `.xzp`, `.zip` or `.xdelta` extension. Reopen the app after copying files.

If you see a patch mismatch message, press A to view the details, then press B to go back and check your patches, cartridge revision and patch order.

If preparation or launch fails, record the error details. When reporting a problem, include the app version, game title and revision, patch names and order, and `nitro-patcher/logs/prepare.log` from the SD card. You can also generate an environment report from the menu; it is saved to `nitro-patcher/logs/environment.log`.
