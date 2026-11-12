# NitroPatcherDS

[English](README.md) | 简体中文

NitroPatcherDS 可以在 3DS／2DS、DSi，以及支持在 NDS 模式下使用 Slot 2 烧录卡的 DS／DS Lite 上为 NDS 游戏卡带应用补丁。游戏运行时从卡带和存储卡读取内容，存档直接读写原卡带。

## 下载安装包

下载对应主机的压缩包。每个包都包含程序、补丁目录、金手指数据库目录和中英文使用说明。

| 主机        | 安装包                | 所需环境                                                                   |
| ----------- | --------------------- | -------------------------------------------------------------------------- |
| 3DS／2DS    | `NitroPatcher3DS.zip` | Luma3DS、FBI                                                               |
| DSi         | `NitroPatcherDSi.zip` | Unlaunch                                                                   |
| DS／DS Lite | `NitroPatcherDS.zip`  | SuperCard miniSD、SuperFW，以及能通过 FlashMe 以 DS 模式启动 Slot-2 的主机 |

## 安装

### 3DS／2DS

需要一台已安装 Luma3DS 和 FBI 的 3DS／2DS。

1. 解压安装包，将 `NitroPatcher3DS.cia` 和 `nitro-patcher` 文件夹复制到 SD 卡根目录。
2. 使用 FBI 安装 CIA。
3. 插入 NDS 游戏卡带，从 HOME 菜单打开 NitroPatcherDS。

### DSi

需要一台已安装 Unlaunch 的 DSi。

1. 解压安装包，将 `nitro-patcher` 文件夹复制到主机 SD 卡根目录。
2. 插入 NDS 游戏卡带，按住 A＋B 开机进入 Unlaunch。
3. 选择 `sdmc:/nitro-patcher/app/NitroPatcherDSi.nds`，按 A 启动。

### DS／DS Lite

需要支持 NDS 模式的 Slot 2 烧录卡。目前已测试安装 SuperFW 的 SuperCard 烧录卡可以使用。

**注意**：NDS／NDS Lite 的运行严重受限，不推荐使用。建议优先在 DSi／3DS／2DS 主机使用。

1. 解压安装包，将 `BOOT.NDS` 和 `nitro-patcher` 文件夹复制到 SuperCard 存储卡根目录。
2. 以 NDS 模式启动 SuperCard，等待 SuperFW 加载程序。
3. 出现卡带提示后插入 NDS 游戏卡带，按 A 识别。运行期间保持 SuperCard 和游戏卡带插入。

## 使用补丁

1. 将补丁直接放入存储卡的 `nitro-patcher/patches/` 文件夹。
2. 使用方向键选择补丁，按 A 启用或取消。
3. 按 START 启动。首次使用时会自动准备缓存，完成后进入游戏；以后可以复用缓存。

详细操作及说明见[使用说明](docs/usage.zh-hans.md)。

## 开源项目

本项目使用 [TWiLight Menu++](https://github.com/DS-Homebrew/TWiLightMenu) 和 [nds-bootstrap](https://github.com/DS-Homebrew/nds-bootstrap) 的代码。

项目采用 [GPL v3](LICENSE)。
