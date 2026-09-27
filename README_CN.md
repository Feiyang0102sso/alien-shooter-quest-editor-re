# QuestEditorRe

[English](README.md)

QuestEditorRe 是任务编辑器 QuestEditor 的重制版本，可独立运行，也可以作为兼容 DLL 由游戏地图编辑器调用。

## 功能

- 编辑 `levels.cfg`、`levels.db` 和 `mission.txt`。
- 支持任务创建、删除、属性编辑、连线、关卡分界及撤销/重做。
- 尽量保留原文件的注释、未知字段、排版和未修改字节。
- 保存前自动创建轮换备份，降低误操作造成的数据损失。
- 提供中文、英文界面，并支持 CP1251、GBK 和系统 ANSI 游戏文本。
- 保留原版 QuestEditor 的 Win32 DLL 接口，可直接接入兼容的地图编辑器。

## 安装与使用

将下列文件放入游戏目录，`QuestEditor.exe` 和 `QuestEditor.dll` 必须位于同一目录：

```text
QuestEditor.exe
QuestEditor.dll
QuestEditor.cfg
```

启动方式：

1. 双击 `QuestEditor.exe`，直接打开当前游戏目录中的任务文件。
2. 在兼容的地图编辑器中按 `Q`，通过 `QuestEditor.dll` 打开编辑器。
3. 也可以向 `QuestEditor.exe` 传入游戏目录、`levels.cfg` 或 `levels.db` 路径。

替换 DLL 前请关闭游戏、地图编辑器和旧版 QuestEditor，并备份原文件。若 `levels.cfg` 和 `levels.db` 同时存在，编辑器优先打开 `levels.cfg`。

## 兼容性

当前兼容目标包括：

- Alien Shooter 2
- Alien Shooter 2 Reloaded 系列
- Zombie Shooter 2
- Alien Shooter 2 Object Extend 1106

不同发行版、汉化版本和 MOD 可能修改任务格式或宿主行为。当前真实游戏自动测试主要基于 AS2R；在其他版本中使用前，建议先在游戏副本中验证。

## 基本操作

| 操作 | 方法 |
|---|---|
| 查看或编辑任务 | 双击任务框，或选中后按 `Enter` |
| 新建任务 | 双击画布空格 |
| 创建连接 | 从一个任务框拖到另一个任务框 |
| 删除连接 | 单击连线后按 `Delete` |
| 删除任务 | 选中任务框后按 `Delete` |
| 多选 | `Ctrl` + 单击，或拖动框选 |
| 平移画布 | 中键拖动，或按住空格后左键拖动 |
| 缩放画布 | 滚轮 |
| 撤销 / 重做 | `Ctrl+Z` / `Ctrl+Y` |
| 保存 | `Ctrl+S` |

属性窗口中的“确定”只会把修改提交到当前编辑会话；仍需在主窗口保存，修改才会写入文件。

奖励页左侧给予物品，右侧移除物品。下拉候选来自当前任务配置同目录的 `Weapon.cfg`，
输入物品 ID 的一部分即可筛选（不区分大小写），选中下拉候选即添加，也支持回车添加。
物品列表每行一个，支持复制粘贴，也接受空格和 `^` 分隔；重复物品保留。
保存时自动转换为游戏使用的空格分隔格式。缺少 `Weapon.cfg` 时仍可手动输入或粘贴。

## 语言与编码

界面支持中文和英文，可通过语言菜单切换。选择结果保存在 `QuestEditor.cfg`：

```ini
; Interface language: 1 = en, 2 = cn
language = 2
```

界面语言与游戏文件编码相互独立。俄文游戏文本使用 CP1251，中文游戏文本通常使用 GBK；纯 ASCII 或无法明确判断时使用系统 ANSI。若显示乱码，请先切换正确的 CFG 或 mission 显示编码，再开始编辑。

## 构建

构建环境：

- Windows 10/11
- Visual Studio C++ 工具集 `v145`
- Windows SDK 10
- C++20
- Win32 平台

使用 Visual Studio 打开 `quest_editor_re.sln`，选择 `Release | Win32` 后构建解决方案。生成文件位于：

```text
bin/Win32/Release/
```

调试独立程序时，将 `QuestEditorLauncher` 设置为启动项目。DLL 必须保持 Win32 构建，不能用 x64 DLL 替换原地图编辑器加载的插件。

## 测试

在项目根目录运行：

```bat
tests\main.bat
```

该脚本会构建 `Release | Win32` 并运行自动测试。也可以只运行指定测试组：

```bat
tests\main.bat unit
tests\main.bat in_game
tests\main.bat in_editor
```

详细说明、测试范围和注意事项见 [tests/README.md](tests/README.md)。

## 数据安全

- 保存前会生成 `.qere.1.bak`、`.qere.2.bak` 轮换备份。
- 编辑器布局保存在 `.qere-layout`，游戏不会读取该文件。
- 修改任务连接、时间线或关卡分界后，应进入游戏验证实际行为。
