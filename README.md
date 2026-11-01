# NitroPatcherDS

English | [简体中文](README.zh-hans.md)

Apply patches to retail Nintendo DS game cartridges on a 3DS, 2DS, DSi, DS or DS Lite. Games read data from both the original cartridge and the storage card, and save directly to the cartridge.

## Download

Download the package for your console. Each package includes the app, a patch folder, and user guides in English and Chinese.

| Console           | Package               | Requirements                                                                |
| ----------------- | --------------------- | --------------------------------------------------------------------------- |
| 3DS / 2DS         | `NitroPatcher3DS.zip` | Luma3DS and FBI                                                             |
| DSi / DSi XL (LL) | `NitroPatcherDSi.zip` | Unlaunch                                                                    |
| DS / DS Lite      | `NitroPatcherDS.zip`  | SuperCard miniSD, SuperFW, and FlashMe configured to boot Slot-2 in DS mode |

## Installation

### 3DS / 2DS

1. Extract the package and copy `NitroPatcher3DS.cia` and the `nitro-patcher` folder to the root of your SD card.
2. Install the CIA using FBI.
3. Insert your Nintendo DS game cartridge and open NitroPatcherDS from the HOME Menu.

### DSi

1. Extract the package and copy the `nitro-patcher` folder to the root of the console's SD card.
2. Insert your Nintendo DS game cartridge and turn on the console while holding A + B to open Unlaunch.
3. Select `sdmc:/nitro-patcher/app/NitroPatcherDSi.nds` and press A to launch it.

### DS / DS Lite

1. Extract the package and copy `BOOT.NDS` and the `nitro-patcher` folder to the root of the SuperCard's storage card. Back up any existing `BOOT.NDS` first.
2. Leave Slot-1 empty and use FlashMe to start the SuperCard in DS mode. Wait for SuperFW to load the app.
3. Insert your Nintendo DS game cartridge when prompted, then press A to read it. Keep both cartridges inserted while playing.

## Using patches

1. Put patch files directly in `nitro-patcher/patches/` on the storage card. Leave XZP packages compressed.
2. Choose your language on first launch. At the cartridge prompt, press A to read the cartridge.
3. Use the directional buttons to highlight a patch, then press A to enable or disable it.
4. Press START to launch the game. The first launch prepares a cache before starting the game. Later launches reuse that cache.

| Button               | Function                                         |
| -------------------- | ------------------------------------------------ |
| Up / Down            | Move the selection                               |
| Left / Right / L / R | Change pages                                     |
| A                    | Enable or disable a patch; confirm a menu choice |
| START                | Launch the game                                  |
| Y                    | View patch information                           |
| X / SELECT           | Open the menu                                    |
| B                    | Go back                                          |

The bottom screen also supports touch controls. The app remembers your language and the enabled patches and their order for each game. Patches are applied in the order you select them, shown by the numbers beside their names. With no patches selected, START launches the original game.

The menu includes per-game settings for built-in anti-piracy patches and skipping the XZP ROM MD5 check. Anti-piracy patches are enabled by default. Skipping MD5 is disabled by default, except when running a DSi-enhanced game on an NDS or DS Lite.

Hold L + R + Down + B for about two seconds during a patched game to return to NitroPatcherDS. Save your game first.

Supported files are `.xzp`, `.zip` packages with the same internal structure as XZP, and whole-ROM `.xdelta` patches. Your cartridge must match the region and revision required by the patch.

3DS and DSi support selecting DS or DSi mode according to the game. DSi-exclusive games and DSiWare are unsupported.

NDS and DS Lite run DSi-enhanced games in DS mode. They can prepare XZP patches for the DS portion of a game, but cannot prepare whole-ROM xdelta patches or XZP packages that require whole-ROM preprocessing for these games. Prepare those caches on a 3DS or DSi, then copy the matching cache directory to the NDS storage card. Use the same cartridge, patch files and patch order. See the [user guide](docs/usage.md) for details.

Caches are stored in `nitro-patcher/cache/`. To free up space, turn off the console and delete this folder. The app will prepare the files again next time. Cartridge save data is unaffected.

See the [user guide](docs/usage.md) for detailed controls, changing cartridges, cache handling and troubleshooting. To build from source, see the [build guide](docs/build.md).

## Open source projects

This project uses the patch format from [NitroPatcher](https://github.com/Xzonn/NitroPatcher) and code from [TWiLight Menu++](https://github.com/DS-Homebrew/TWiLightMenu) and [nds-bootstrap](https://github.com/DS-Homebrew/nds-bootstrap).

Licensed under [GPL v3](LICENSE). Related license texts are available in the app under About → Licenses.
