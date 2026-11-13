# NitroPatcherDS User Guide

NitroPatcherDS applies patches to Nintendo DS game cartridges on a 3DS, 2DS, DSi, or a DS / DS Lite with a Slot-2 flashcart that supports DS mode. Games read data from both the cartridge and the storage card, and save directly to the original cartridge.

## Installation

### 3DS / 2DS

You need a 3DS or 2DS with Luma3DS and FBI installed.

1. Extract `NitroPatcher3DS.zip` and copy `NitroPatcher3DS.cia` and the `nitro-patcher` folder to the root of your SD card.
2. Put your patch files in `nitro-patcher/patches/` on the SD card.
3. Put the SD card back in your console and install `NitroPatcher3DS.cia` using FBI.
4. Insert your game cartridge and open the app from the HOME Menu.

### DSi

You need a DSi with Unlaunch installed.

1. Extract `NitroPatcherDSi.zip` and copy the `nitro-patcher` folder to the root of the console's SD card.
2. Put your patch files in `nitro-patcher/patches/`.
3. Put the SD card back in your console.
4. Insert the game cartridge and turn on the console while holding A + B to open Unlaunch's Filemenu. Select NitroPatcherDS and press A to launch it.
5. To assign a startup shortcut, open Unlaunch's `OPTIONS`, assign the app to one of the `BUTTON A / B / X / Y` options, then choose `SAVE & EXIT`. Hold that button while turning on the console to open the app.
6. You can also launch it from TWiLight Menu++.

### NDS / NDS Lite

You need a Slot-2 flashcart that supports DS mode. SuperCard flashcarts running SuperFW have been tested and work.

**Note**: Operation on DS / DS Lite is severely limited and is not recommended. Use a DSi, 3DS or 2DS if possible.

1. Extract `NitroPatcherDS.zip` and copy `BOOT.NDS` and the `nitro-patcher` folder to the root of the flashcart's storage card.
2. Put your patch files in `nitro-patcher/patches/`.
3. Start the SuperCard in DS mode. With FlashMe installed, you can start it directly from the console menu. You can also use a Slot-1 flashcart that supports booting Slot-2.
4. Insert the game cartridge into Slot-1. SuperFW automatically loads `BOOT.NDS` and starts the app.
5. Keep the Slot-2 flashcart inserted while playing.

## Supported patches

| Extension | Contents                                                |
| --------- | ------------------------------------------------------- |
| `.xzp`    | A NitroPatcher patch package                            |
| `.xdelta` | An xdelta/VCDIFF patch made for a complete ROM          |

Check that your cartridge matches the game region and revision required by the patch.

### DSi-enhanced games on NDS / DS Lite

DS and DS Lite can run the DS portion of DSi-enhanced games such as Pokémon Black, White, Black 2 and White 2. When preparing an XZP patch, the app displays a notice that the game will run in DS mode.

An NDS cannot read the complete DSi region, so **Skip ROM MD5** is enabled by default for these games. If you disable this option, use a cache prepared with ROM MD5 verification on a 3DS or DSi.

Whole-ROM `.xdelta` patches and XZP packages that include whole-ROM preprocessing for these games must be prepared on a 3DS or DSi:

1. On the 3DS or DSi, use the same cartridge, select the same patch files in the same order, and complete preparation.
2. Turn off the console and copy the matching cartridge and patch combination directory under `nitro-patcher/cache/` to the same location on the Slot-2 flashcart's storage card.
3. On the NDS, select the same patches in the same order and press START to reuse the cache and launch in DS mode.

## Selecting patches and starting a game

The app reads the game cartridge automatically at startup. If no cartridge is inserted, it prompts you to insert one and press A. Once it has been read, the top screen shows the game icon and title, and the bottom screen lists your patches.

Use Up and Down to move through the list, press A to enable or disable a patch, then press START to launch the game. Press Y on a patch to view its information.

When you use a patch for the first time, the app prepares the required files. This may take a while, and the game starts automatically when preparation is complete. The app remembers the enabled patches and their order for each game. Later launches reuse the cache and start much faster.

To change cartridges, press X to open the menu and select **Read another Game Card**. Insert the cartridge as prompted, then press A.

### Using multiple patches

Patches are applied in the order you select them. The numbers `[1]`, `[2]` and so on beside their names show this order. To change the order, deselect the patches and select them again in the order you need.

Follow the patch authors' instructions when combining patches. For example, the Japanese-name patch for the fourth-generation Pokémon games requires a Chinese translation as its base, so select the translation patch first, followed by the name patch. An incorrect order may cause a preparation error.

## Cheats

1. Put a TWiLight Menu++ compatible `usrcheat.dat` in `nitro-patcher/cheats/` on the storage card.
2. Select your patches, then open X → Cheats. The app needs to prepare the patches and calculate a checksum before opening the cheat list.
3. Press A to open a category or enable or disable a code, Y to read its description, and SELECT to disable all codes.
4. Return to the patch list and press START to launch.

## Buttons and touch controls

| Control              | Function                                                                |
| -------------------- | ----------------------------------------------------------------------- |
| Up / Down            | Move the selection                                                      |
| Left / Right / L / R | Change pages                                                            |
| A                    | Enable or disable a patch in the patch list; confirm a menu choice      |
| START                | Launch the game from the patch list                                     |
| X / SELECT           | Open the menu from the main screen                                      |
| Y                    | View information and a description for the highlighted patch            |
| B                    | Go back when indicated on screen                                        |

### In-game controls

| Control                                    | Function                    |
| ------------------------------------------ | --------------------------- |
| Hold L + R + Down + B for about two seconds | Return to NitroPatcherDS     |
| L + Down + SELECT                          | Open the nds-bootstrap menu |

## Menu

- **Cheats**: Select codes for the current game and patch combination.
- **Current game AP patches**: Choose Default, Disabled or Forced. Saved per game. Default automatically matches the game and checksum; Disabled turns off built-in anti-piracy patches; Forced ignores the checksum and uses a patch for that game, selecting the first one if several are available. Some translations, such as Tomodachi Collection, already include anti-piracy fixes and require this setting to be disabled to avoid errors.
- **Skip ROM MD5**: Skip the `md5.txt` match requirement. Disabled by default, except when reading DSi-enhanced games on NDS / DS Lite. Even with this option enabled, applying an xdelta file may still require reading the entire ROM.
- **Max files open at once**: Choose Unlimited or 16. Unlimited by default and saved per game. If patch preparation fails to open a file, try setting this to 16. Preparation may take longer.
- **Synchronous read wait**: On 3DS / DSi, choose Automatic, Enabled or Disabled. Automatic by default and saved per game. Try enabling this if a game displays flickering, corrupted graphics or content from the wrong screen.
- **Start original**: Launch the current cartridge with patches and cheats disabled.
- **Read another Game Card**: Change the current game.
- **Clear current game cache**: Delete cached files for the current game. Try this to free up space or if the existing cache causes errors.
- **Environment report**: Create a report about the console and SD card to help diagnose problems.
- **Interface language**: Multiple languages are supported, including Simplified Chinese, Traditional Chinese, Japanese, English and Korean. Press A to save your choice, or B to restore the previous language.
- **Repair memory card**: Check allocation chains, FAT copies and free space across the storage card.
- **About**: View the app version, author and open source projects used. Press A to read the licenses.

## Cache

After preparation, the cache is stored in `nitro-patcher/cache/` on the SD card. It is reused when you use the same cartridge revision and patches with the same contents and order. Changing patch contents or the combination of patches creates a corresponding cache. Cached files can be reused indefinitely.

To free up space, turn off the console and delete `nitro-patcher/cache/`. The app will prepare the files again the next time you launch a patched game. Save data is stored on the cartridge, so deleting the patch cache does not delete your saves.

## Troubleshooting

If no patches appear, check that the files are directly inside `nitro-patcher/patches/` and have a `.xzp` or `.xdelta` extension. Reopen the app after copying files.

If you see a patch mismatch message, press A to view the details, then press B to go back and check your patches, cartridge revision and patch order.

If an error occurs after launch, try disabling built-in anti-piracy patches for translations that already include AP fixes. If a game needs an AP patch but automatic matching fails, try Forced.

If patch preparation fails to open a file, set **Max files open at once** to 16 in the menu. For flickering or corrupted graphics on 3DS / DSi, try enabling **Synchronous read wait**.

If preparation or launch fails, take a photo of the error details. When reporting a problem, include the app version, game title and revision, patch names and order, and an archive of the `nitro-patcher` folder from the SD card.
