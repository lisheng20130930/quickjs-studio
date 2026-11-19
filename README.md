# QuickJS Studio

QuickJS Studio 是一个面向嵌入式 QuickJS 程序的轻量 Windows JavaScript IDE。界面采用经典 VC6 布局，使用 MinGW64 构建；它不依赖 VS Code、DAP、Node.js，也不需要运行额外的调试服务器。

当前版本提供：

- `.qsp` 工程、脚本目录树和 JavaScript 语法高亮；可新建/打开/关闭工程；
- FileView 支持新建脚本、新建目录、加入已有文件、重命名、刷新和从工程排除；
- 查找、继续查找、工程内查找、替换、跳行、书签和插入文件文本；
- F9 源码断点；
- F5 启动或继续，Shift+F5 停止；
- F10 单步跳过，F11 单步进入，Shift+F11 单步跳出；
- Call Stack、Locals、Watch 和 Output 窗口；
- 编辑器右键 `Add to Watch`，Watch 表达式在每次暂停时自动刷新；
- 编辑器上方采用 Visual Assist Navigation Bar：Context Field 显示光标下的符号，Definition Field 显示其真实定义；Context 下拉列表可浏览当前文件的方法；
- 光标放在函数调用、方法或类名上，点击 `Go`、按 F12 或使用右键 `Go to Definition`，可直接跳到同文件或跨文件实现；
- 编辑器采用 Visual Assist 风格的增强语义着色：类型/类为蓝色、变量为深蓝色、函数/方法为棕色、全大写常量为紫红色，同时保留系统豆沙绿背景；
- JavaScript 自动补全：输入两个标识符字符自动弹出候选，输入 `.` 弹出常用成员，`Ctrl+Space` 可随时强制补全；候选综合当前文件、整个工程和常用 JavaScript API；
- 宿主 API 声明：通过 `.qsp` 的 `HostApi` 指定 `.api.js` 文件，为 `Engine.log()` 一类 QuickJS 宿主接口提供精确成员补全、定义显示和跳转；使用方法见 [宿主 API 声明](docs/HOST-API.md)；
- Workspace 提供 ClassView 和 FileView，支持类、方法、全局符号和目录层级导航；
- 工程树、编辑器和底部调试窗口之间的分隔线可拖动调整；
- 编辑器统一使用四空格缩进：Tab 插入四个空格，Backspace 按缩进级别回退；
- 通过本机命名管道启动并调试任意嵌入 Debug Agent 的 EXE；
- 目标程序的 stdout/stderr 直接显示在 IDE 的 Output 窗口。

它只支持“按 F5 启动 EXE 调试”，不实现 Attach、远程调试、插件系统和复杂工作区。这是有意保持的边界。

## 目录

```text
agent/                  给 QuickJS 宿主使用的 Debug Agent
docs/                   使用说明和界面参考资料
examples/               最小宿主、脚本和示例工程
scripts/                Windows 构建与测试脚本
src/                    Win32 IDE 的实现
tests/                  调试、符号和工程模型回归测试
third_party/quickjs/    工程自带的 QuickJS 和调试钩子
third_party/scintilla/  编辑器控件
third_party/lexilla/    JavaScript 词法着色
third_party/cjson/      调试协议 JSON
```

所有依赖均在本工程内，不引用相邻的 `min-browser-master` 或 `framework-master`。

### 源码职责

`src/` 按职责组织，不再把所有功能堆在窗口入口中：

```text
main.c                   Win32 入口、消息循环和命令分发
studio_ui.c              菜单、工具栏、控件创建、布局和分隔条
studio_navigation.c      工程符号、ClassView、Go、补全和源码切换
studio_workspace.c       工程生命周期、FileView 和文件编辑命令
studio_debug.c           断点、启动/单步、Watch、Locals 和 Call Stack
studio_internal.h        上述控制模块之间的内部契约，不对宿主发布

editor.c/.h              Scintilla 编辑器封装
project.c/.h             .qsp 工程模型和持久化
symbol_search.c/.h       不依赖 UI 的 JavaScript 符号扫描
debug_session.c/.h       EXE、命名管道和调试协议传输
input_dialog.c/.h        通用单值输入对话框
project_settings.c/.h    工程设置对话框
```

依赖方向保持为：`main/studio_*` 控制层调用下方独立模块；`editor`、`project`、`symbol_search` 和 `debug_session` 之间不互相引用 UI。新增功能应优先放进对应模块，不要把业务逻辑重新堆回 `main.c`。

## 构建

### 在当前 Linux 虚拟机交叉构建 Windows 版本

需要安装 64 位 MinGW-w64，其中 C++ 编译器使用 POSIX thread variant。

```bash
make windows
```

生成文件：

```text
build/windows/bin/quickjs-studio.exe
build/windows/bin/qjs-host.exe
build/windows/bin/Scintilla.dll
build/windows/bin/lexilla.dll
```

这四个文件放在同一目录即可运行，不依赖额外的 MinGW DLL。

调试内核的 Linux 回归测试：

```bash
make test
```

### 在 Windows 上构建

默认使用 `D:\mingw64`。在工程根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

如果 MinGW64 安装在其他目录：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 `
    -MinGWRoot "C:\工具\mingw64"
```

构建并运行调试内核回归测试：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test-windows.ps1
```

Windows 构建脚本还需要 `python` 命令，用来构建工程内的 QuickJS 静态库。测试脚本同时验证调试器、WizardBar 的 JavaScript 符号识别，以及 `.qsp` 的创建、设置、排除和重新加入。

## VC6 界面对应关系

界面职责按 Visual C++ 6.0 手册组织，而不是只模仿截图：

- 菜单栏保持 `File / Edit / View / Insert / Project / Build / Tools / Window / Help` 的经典顺序，只保留 QuickJS Studio 中真实可执行的命令；
- Standard 与 Debug 常用操作集中在顶部小图标工具栏，分组包括打开/保存、剪切/复制/粘贴、启动/停止和单步；
- Visual Assist Navigation Bar 的 Context Field 跟随光标符号，Definition Field 展示源码定义；不在符号上时显示当前函数作用域。Context 下拉列表按源码顺序列出当前文件定义，选择后直接跳转；
- Workspace 左侧以底部 `ClassView / FileView` 标签切换。ClassView 按 class、方法和 Globals 组织，FileView 保留真实目录层级；
- 底部 Output 与调试窗格使用底部标签；Standard/Debug 工具栏、WizardBar、Workspace、Output 和状态栏都可从 View 菜单独立显示或隐藏；
- 状态栏显示当前状态、行、列和插入模式。左右及上下分隔条均可拖动。

这一版不是只照一张截图拼外观。菜单结构、Workspace 语义和常用快捷键交叉核对了以下资料：

- [Microsoft Visual C++ 6.0 Programmer's Guide](https://bitsavers.trailing-edge.com/pdf/microsoft/visual_c%2B%2B/Visual_C%2B%2B_6.0_1999/097-0001981_Visual_C%2B%2B_6.0_Programmers_Guide_1998.pdf)；
- [Special Edition Using Visual C++ 6：The Visual Studio User Interface, Menus, and Toolbars](https://docs.huihoo.com/windows/special-edition-using-vc6/apc/apc.htm)；
- [Learn Microsoft Visual C++ 6.0 Now!](https://bitsavers.trailing-edge.com/pdf/microsoft/visual_c%2B%2B/Visual_C%2B%2B_6.0_1999/Sphar_Learn_Microsoft_Visual_C%2B%2B_6.0_Now_1999.pdf) 的 FileView/ClassView 章节；
- [DigiPen Visual C++ 6.0 Guide](https://azrael.digipen.edu/~mmead/www/docs/VisC-Guide.pdf)；
- [WPU Program Development with Microsoft Visual C/C++ 6](https://www.wpunj.edu/cosh/departments/computer-science/assets-cs/Microsoft_Visual_C%2B%2B_6.0_Tutorial.pdf) 教程及 FileView 右键截图。

MFC、资源编辑器、C/C++ 编译参数和 SourceSafe 等与 QuickJS 调试无关的功能没有伪造；其余能映射到 JavaScript 工程的主流程均提供真实行为。

## 工程和文件操作

- `File > New Project...` 创建 `.qsp` 和默认 `main.js`，调试目标默认使用 IDE 同目录的 `qjs-host.exe`；
- `Project > Add To Project > New JavaScript File...` 在当前 FileView 目录中创建脚本；
- `Project > Add To Project > New Folder...` 输入目录名并在当前目录中创建真实子目录；
- `Add Files to Folder...` 把一个或多个已有文件复制到当前目录；目标已存在时会先询问是否覆盖；
- FileView 文件或目录可通过右键或 F2 重命名；
- FileView 的 `Remove From Project` 或 Delete 只从工程隐藏，绝不会删除磁盘内容；排除项保存在 `.qsp` 的 `[Excluded]` 节中；再次通过 `Add Files to Folder...` 加入同一路径即可恢复；
- `Project > Settings...`（Alt+F7）可修改调试 EXE、参数、工作目录、脚本根目录和入口脚本；
- `Edit > Find in Files...` 在当前工程所有未排除的 JavaScript 文件中搜索，并把 `文件(行号): 内容` 输出到底部 Output 窗口；双击结果可直接跳到源码行。

## 运行示例

直接启动：

```powershell
.\build\windows\bin\quickjs-studio.exe
```

IDE 会打开 `examples/quickjs-studio.qsp`。也可以显式传入工程文件：

```powershell
.\build\windows\bin\quickjs-studio.exe .\examples\quickjs-studio.qsp
```

示例工程包含根目录的 `game.js`，以及 `game/player.js`、`game/update.js`、`math/distance.js` 等子目录脚本。左侧工程树会保留目录层级；`player.js` 用于展示 ClassView 和 WizardBar 的 class/方法导航。

在 `game/update.js` 的 `const distance = ...` 行按 F9，然后按 F5。程序会停在断点处：

- Call Stack 显示 `update()`；
- Locals 显示 `deltaTime`、`speed` 等局部变量；
- 在编辑器中选中 `speed * deltaTime`，右键选择 `Add to Watch`，即可添加常驻表达式；也可在 Watch 输入框手工添加。每次暂停都会自动刷新，选中后按 Delete 可删除；
- F10、F11、Shift+F11 和 F5 分别执行单步和继续。

## 工程文件

`.qsp` 是简单 INI 文件，所有相对路径均相对于工程文件所在目录：

```ini
[Project]
Name=My Game
Executable=../build/windows/bin/game.exe
Arguments="$(Entry)"
WorkingDirectory=.
ScriptRoot=scripts
Entry=scripts/main.js
HostApi=api/engine.api.js
```

`HostApi` 可省略；需要补全宿主注册接口时，按 [宿主 API 声明使用说明](docs/HOST-API.md) 建立声明文件。

从工程排除的文件或目录由 IDE 自动维护，例如：

```ini
[Excluded]
Path1=.\scripts\legacy
Path2=.\scripts\unused.js
```

支持两个参数宏：

- `$(ProjectDir)`：工程文件目录的绝对路径；
- `$(Entry)`：入口脚本的绝对路径。

## 接入自己的 QuickJS EXE

如果只需要运行 JavaScript，宿主可以继续使用标准 QuickJS，不受 QuickJS Studio 限制。

如果需要在 QuickJS Studio 中使用断点、单步、Call Stack、Locals 和 Watch，目标程序必须：

1. 使用本工程 `third_party/quickjs` 构建的调试版静态库；
2. 编译并链接 `agent/qjs_debug_agent.c`；
3. 在执行第一段 JavaScript 前连接 Debug Agent。

标准 QuickJS 没有解释器调试钩子，因此只加入 Agent 文件、不替换静态库无法调试。目标程序仍然是你自己的 EXE，不要求使用示例 `qjs-host.exe`，已有的游戏逻辑和 bindings 也不需要搬入 IDE。

创建 `JSContext`、安装业务 bindings 后，在执行第一段 JavaScript 前调用：

```c
#include "qjs_debug_agent.h"

JSRuntime *runtime = JS_NewRuntime();
JSContext *context = JS_NewContext(runtime);

install_my_engine_bindings(context);

if (qjs_debug_agent_connect_from_environment(context) < 0) {
    /* IDE 请求了调试，但命名管道连接失败。 */
    return 1;
}

JS_Eval(context, source, source_size, source_name, JS_EVAL_TYPE_GLOBAL);
```

不从 QuickJS Studio 启动时，没有 `QJS_STUDIO_PIPE` 环境变量，Agent 会直接返回 `0`，不会改变程序正常运行方式。

`source_name` 必须稳定且能与 IDE 中的脚本名对应。普通脚本建议使用绝对文件名。调试网页脚本时，建议使用：

- 外部脚本：完整 URL；
- 页面内联脚本：`页面URL#inline-1`、`页面URL#inline-2`。

当前 IDE 已完成本地脚本调试闭环。`min-browser.exe` 的网页源码注册和虚拟脚本显示会复用同一调试协议，不需要另造一套调试器。

## 实现边界

- IDE 本身不链接 QuickJS；QuickJS 只存在于被调试的目标 EXE 中。
- IDE 和目标程序通过仅限本机的 Windows named pipe 通信。
- Debug Agent 在执行首段 JavaScript 前停在 entry，IDE 下发断点后再继续，因此不会漏掉入口脚本断点。
- Scintilla、Lexilla 和 QuickJS 均以源码随工程保存，版本升级由本工程控制。

