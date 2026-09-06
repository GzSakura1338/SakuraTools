# SakuraTools

Minecraft 1.20.1 的 C++ 客户端代理。A 持有远端连接，B 连接本地 25565 端口；中途接入时先同步当前世界，再交接操作。

## 开发入口

```powershell
./scripts/build.ps1
```

脚本自动定位 Visual Studio（需要 C++ 和 CMake 组件），配置、编译并运行测试。产物为 `proxy/Meadow.dll`、`proxy/Canvas.exe` 和 `proxy/Ribbon.dll`，测试产物留在构建目录。现有 JVM 测试需要 JDK 17 或更新版本；仅运行纯 C++ 测试可加 `-WithoutJvmTests`。

构建脚本支持 `-BuildDir`、`-Configuration Debug`、`-VisualStudio` 和 `-SkipTests`。正常运行从 `proxy/launcher.ps1` 进入；`scripts/inject.ps1` 只代理到 `proxy/inject.ps1`，注入逻辑仅维护一份。

维护前先阅读 [架构与维护说明](docs/architecture.md)。服务器公开接口在 `native/b_server.h`，内部实现位于 `native/server/`；新增生产源码只需登记到 `native/sources.txt`，CMake 与 Zig 脚本读取同一份清单。

`scripts/build.sh` 是 Zig 交叉编译入口；`build_bare.sh`、`build_mindll.sh` 和 `mindll/` 用于底层实验，不是日常发布入口。`skid/` 是 Minecraft 参考源码，不参与生产构建。

所有构建入口的 DLL/EXE 成品统一放在项目根目录的 `proxy/`（本机为 `D:\AI\Minecraft\SakuraTools\proxy`），包括实验用 `Meadow_bare.dll`、`Pebble.dll`。Debug/Release 不额外创建输出子目录；编译中间文件和测试产物仍留在 `build/`。

## DEL 停用与再次启用

- A 客户端窗口在前台时，松开再按下 DEL 可停用代理；长按只触发一次。
- 停用会断开 B、关闭 25565 监听和局域网广播，并撤销连接 Hook 与转发处理器，保留 A 的远端连接。
- DLL 与控制线程继续驻留。再次运行 `Canvas.exe Meadow.dll` 会请求恢复，正常启停没有次数限制。
- 停用过程会自动重试未完成的清理。清理期间收到的恢复请求会保留，清理成功后再启动。
- 再次启用复用进程中的 DLL；替换磁盘 DLL 后，需要重启 A 才会加载新版本。
- 日志优先位于 `%APPDATA%/.minecraft/proxy.log`，无法写入时回退到 `%TEMP%/MinecraftProxy.log`。`STOP: suspended` 表示清理完成，`START: active` 表示代理已启动。

## A 已入服后注入

中途接入使用 C++ JNI 在 A 的游戏主线程读取当前世界，向 B 发送登录信息、已加载区块和光照、玩家列表、队伍、原版实体、背包、出生点与真实位置。主线程调度沿用 C++ 生成的 native `run()` 桥接，不需要新增 Java 源文件或辅助 JAR。

快照期间暂存实时数据包，快照发送后按序补发；B 确认本地初始化传送后才接管操作。这个确认不会发往远端服务器。快照失败、缓存溢出或初始化超时会关闭 B，保留 A 的连接和控制权。快照只覆盖 A 当前已加载的数据；需要 Forge 自定义握手的模组实体不在当前支持范围内。

复测时先重启 A，进入远端服务器，再注入新 `proxy/Meadow.dll`，最后让 B 加入代理。日志应依次出现 `native snapshot chunks=...`、`snapshot sent` 和 `B acknowledged world snapshot`。检查 B 能进入当前区块、位置与 A 一致、移动无持续拉回，并覆盖 B 断开后重新加入的情况。

## 验证

在 Visual Studio x64 开发环境中：

```powershell
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/msvc
ctest --test-dir build/msvc --output-on-failure
```

`runtime_gate` 测试覆盖 DEL 边沿、后台按键、10000 次回调门控启停、超时重试和并发排空；不代替游戏内端到端测试。

`login_handoff` 覆盖快照状态切换、本地传送确认过滤、失败后的迟到确认及截止时间。另有 C++ JNI 测试，使用本机 Minecraft 1.20.1 文件检查关键字段映射和数据包编码解码，不启动游戏窗口：

```powershell
./scripts/test_snapshot_native.ps1 -JavaHome 'D:/Program Files/Java/jdk-21.0.12'
```

此测试使用原版 SRG 类和资源，不能替代 Forge 启动器环境中的双客户端验证。

游戏内验证：A 与 B 建立转发后，在 A 按 DEL，确认 B 断开、25565 关闭且 A 可以继续操作；再次运行注入器并重连 B，确认双向转发恢复。重复测试，并分别覆盖 B 尚未连接、A 已进入服务器、快速重复请求恢复的情况。
