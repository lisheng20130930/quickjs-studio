# 宿主 API 声明使用说明

QuickJS Studio 可以通过一个独立的 JavaScript 声明文件识别宿主程序注册的全局 API。声明文件只用于编辑器的自动补全、定义显示和代码跳转，不会被 IDE 注入或执行。

## 1. 建立声明文件

建议在工程内建立 `api` 目录，并使用 `.api.js` 后缀：

```text
my-game/
├── game.qsp
├── main.js
└── api/
    └── engine.api.js
```

假设宿主使用 QuickJS 注册了 `Engine.log()`、`Engine.drawSprite()` 和 `Engine.loadTexture()`，声明文件可以写成：

```js
class Engine {
    static log(text) {}

    static drawSprite(texture, x, y) {}

    static loadTexture(path) {}
}
```

类名必须与宿主暴露的全局对象名一致，方法名和参数顺序也应与实际接口一致。方法体保持为空即可。

多个宿主对象可以放在同一文件：

```js
class Engine {
    static log(text) {}
}

class Audio {
    static play(path, volume) {}

    static stopAll() {}
}
```

## 2. 在工程中配置

编辑 `.qsp` 文件，在 `[Project]` 中增加 `HostApi`：

```ini
[Project]
Name=My Game
Executable=build/game.exe
Arguments="$(Entry)"
WorkingDirectory=.
ScriptRoot=.
Entry=main.js
HostApi=api/engine.api.js
```

`HostApi` 相对于 `.qsp` 所在目录解析，也可以填写绝对路径。还可以通过 `Project > Settings...` 的 `Host API file` 设置。

## 3. 编辑器中的效果

配置完成并重新打开工程后：

- 输入 `Eng`，自动补全中出现 `Engine`；
- 输入 `Engine.`，候选列表只显示 `Engine` 声明的方法；
- 光标放在 `Engine.log` 的 `log` 上，Context Field 显示该符号，Definition Field 显示 `static log(text) {}`；
- 点击 `Go`、按 `F12` 或使用右键 `Go to Definition`，跳到 `engine.api.js` 中的声明位置；
- 保存声明文件后，工程符号和补全候选会自动刷新。

## 4. 声明文件不会参与运行

QuickJS Studio 不会自动加载 `HostApi`。宿主仍然只执行 `.qsp` 的 `Arguments` 和 `Entry` 所指定的脚本。

不要在业务脚本中 `import` 声明文件，也不要把它加入传给宿主的脚本参数。这样声明中的空类不会覆盖宿主实际注册的全局对象。

## 5. 维护原则

- 宿主增加、删除或修改 API 时，同步修改声明文件；
- 声明文件应与宿主代码一起提交到版本库；
- 声明只描述公开给脚本使用的接口，不需要复制 C 实现；
- 参数名应使用业务含义明确的名称，它们会直接显示在 Definition Field 中；
- 大型宿主可以按模块维护声明，再生成一个汇总的 `engine.api.js`。

## 6. 当前边界

当前版本支持全局宿主对象及其一级方法，例如 `Engine.log()`。尚不推断函数返回值的实例类型，因此 `Engine.loadTexture(...).width` 这类链式对象的成员补全需要后续增加类型推断。

IDE 不扫描 `JS_SetPropertyStr()` 等 C 注册代码，也不依赖正在运行的宿主进程。声明文件是补全信息的唯一来源，因此行为稳定，也不会因为启动游戏或连接调试器而产生副作用。
