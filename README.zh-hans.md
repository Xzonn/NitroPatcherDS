# NitroPatcherDS

[English](README.md) | 简体中文

为 NDS 正版卡带应用汉化等补丁，支持 3DS／2DS、DSi 和 DS／DS Lite。游戏运行时从原卡带和存储卡读取内容，存档直接读写原卡带。

## 下载安装包

下载对应主机的压缩包。每个包都包含程序、补丁目录和中英文使用说明。

| 主机              | 安装包                | 所需环境                                                                   |
| ----------------- | --------------------- | -------------------------------------------------------------------------- |
| 3DS／2DS          | `NitroPatcher3DS.zip` | Luma3DS、FBI                                                               |
| DSi／DSi LL（XL） | `NitroPatcherDSi.zip` | Unlaunch                                                                   |
| DS／DS Lite       | `NitroPatcherDS.zip`  | SuperCard miniSD、SuperFW，以及能通过 FlashMe 以 DS 模式启动 Slot-2 的主机 |

## 安装

### 3DS／2DS

1. 解压安装包，将 `NitroPatcher3DS.cia` 和 `nitro-patcher` 文件夹复制到 SD 卡根目录。
2. 使用 FBI 安装 CIA。
3. 插入 NDS 游戏卡带，从 HOME 菜单打开 NitroPatcherDS。

### DSi

1. 解压安装包，将 `nitro-patcher` 文件夹复制到主机 SD 卡根目录。
2. 插入 NDS 游戏卡带，按住 A＋B 开机进入 Unlaunch。
3. 选择 `sdmc:/nitro-patcher/app/NitroPatcherDSi.nds`，按 A 启动。

### DS／DS Lite

1. 解压安装包，将 `BOOT.NDS` 和 `nitro-patcher` 文件夹复制到 SuperCard 存储卡根目录。已有 `BOOT.NDS` 时先备份原文件。
2. 保持 Slot-1 为空，通过 FlashMe 以 DS 模式启动 SuperCard，等待 SuperFW 加载程序。
3. 出现卡带提示后插入 NDS 游戏卡带，按 A 识别。运行期间保持 SuperCard 和游戏卡带插入。

## 使用补丁

1. 将补丁直接放入存储卡的 `nitro-patcher/patches/` 文件夹，无需解压 XZP 补丁包。
2. 首次打开程序时选择语言；在卡带提示页面按 A 识别卡带。
3. 使用方向键选择补丁，按 A 启用或取消。
4. 按 START 启动。首次使用时会自动准备缓存，完成后进入游戏；以后可以复用缓存。

| 按键             | 功能                         |
| ---------------- | ---------------------------- |
| 方向键上下       | 移动选择                     |
| 方向键左右／L／R | 翻页                         |
| A                | 选择或取消补丁，确认菜单选项 |
| START            | 启动游戏                     |
| Y                | 查看补丁信息                 |
| X／SELECT        | 打开菜单                     |
| B                | 返回                         |

下屏幕也支持触屏操作。程序会保存界面语言，以及每个游戏启用的补丁和顺序。多个补丁按选择顺序应用，列表中的数字表示顺序；没有选择补丁时，START 启动原版游戏。

菜单中可以按游戏设置是否开启内置反烧录补丁和是否跳过 ROM MD5 校验。反烧录补丁默认开启；跳过 MD5 默认关闭，NDS／DS Lite 运行 DSi 增强游戏时默认开启。

运行打过补丁的游戏时，同时按住 L＋R＋↓＋B 约两秒，可以返回 NitroPatcherDS。返回前请先在游戏中保存。

支持 `.xzp`、内部结构与 XZP 相同的 `.zip`，以及整份 ROM 的 `.xdelta` 补丁。请确认补丁要求的游戏地区和修订版本与卡带一致。

3DS 和 DSi 支持根据游戏类型选择 DS 或 DSi 模式。DSi 独占游戏和 DSiWare 不在支持范围内。

NDS／DS Lite 以 DS 模式运行 DSi 增强游戏，可以准备游戏 DS 部分的 XZP 补丁。这类游戏的整份 ROM xdelta 补丁，以及需要整份 ROM 预处理的 XZP 补丁，需在 3DS／DSi 上准备缓存，再将对应的缓存目录复制到 NDS 存储卡。两边需要使用同一张卡带、相同补丁文件和启用顺序，详见[使用说明](docs/usage.zh-hans.md)。

缓存保存在 `nitro-patcher/cache/`。需要释放空间时，关闭主机后删除此目录，下次使用时会重新准备。卡带中的游戏存档不受影响。

详细操作、更换卡带、缓存说明和问题反馈见[使用说明](docs/usage.zh-hans.md)。从源码构建请参阅[构建说明](docs/build.zh-hans.md)。

## 开源项目

本项目沿用 [NitroPatcher](https://github.com/Xzonn/NitroPatcher) 的补丁格式，并使用 [TWiLight Menu++](https://github.com/DS-Homebrew/TWiLightMenu) 和 [nds-bootstrap](https://github.com/DS-Homebrew/nds-bootstrap) 的代码。

项目采用 [GPL v3](LICENSE)。程序的“关于 → 授权协议”菜单中可以查看相关授权协议。
