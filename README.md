# NitroPatcherDS

English | [简体中文](README.zh-hans.md)

NitroPatcherDS applies patches to Nintendo DS game cartridges on a 3DS, 2DS, DSi, or a DS / DS Lite with a Slot-2 flashcart that supports DS mode. Games read data from both the cartridge and the storage card, and save directly to the original cartridge.

## Download

Download the package for your console.

- GitHub: <https://github.com/Xzonn/NitroPatcherDS/releases/latest>

| Console      | Package               | Requirements                                                                |
| ------------ | --------------------- | --------------------------------------------------------------------------- |
| 3DS / 2DS    | `NitroPatcher3DS.zip` | Luma3DS and FBI                                                             |
| DSi          | `NitroPatcherDSi.zip` | Unlaunch                                                                    |
| DS / DS Lite | `NitroPatcherDS.zip`  | SuperCard miniSD, SuperFW, and FlashMe configured to boot Slot-2 in DS mode |

## Installation

### 3DS / 2DS

You need a 3DS or 2DS with Luma3DS and FBI installed.

1. Extract the package and copy `NitroPatcher3DS.cia` and the `nitro-patcher` folder to the root of your SD card.
2. Install the CIA using FBI.
3. Insert your Nintendo DS game cartridge and open NitroPatcherDS from the HOME Menu.

### DSi

You need a DSi with Unlaunch installed.

1. Extract the package and copy the `nitro-patcher` folder to the root of the console's SD card.
2. Insert your Nintendo DS game cartridge and turn on the console while holding A + B to open Unlaunch.
3. Select `sdmc:/nitro-patcher/app/NitroPatcherDSi.nds` and press A to launch it.

### DS / DS Lite

You need a Slot-2 flashcart that supports DS mode. SuperCard flashcarts running SuperFW have been tested and work.

**Note**: Operation on DS / DS Lite is severely limited and is not recommended. Use a DSi, 3DS or 2DS if possible.

1. Extract the package and copy `BOOT.NDS` and the `nitro-patcher` folder to the root of the SuperCard's storage card.
2. Start the SuperCard in DS mode and wait for SuperFW to load the app.
3. Insert your Nintendo DS game cartridge when prompted, then press A to read it. Keep both cartridges inserted while playing.

## Using patches

1. Put patch files directly in `nitro-patcher/patches/` on the storage card.
2. Use the directional buttons to highlight a patch, then press A to enable or disable it.
3. Press START to launch the game. The first launch prepares a cache before starting the game. Later launches reuse that cache.

See the [user guide](docs/usage.md) for detailed instructions.

## Open source projects

This project uses code from [TWiLight Menu++](https://github.com/DS-Homebrew/TWiLightMenu) and [nds-bootstrap](https://github.com/DS-Homebrew/nds-bootstrap).

Licensed under [GPL v3](LICENSE).
