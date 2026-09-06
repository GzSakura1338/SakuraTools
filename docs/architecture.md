# 架构与维护

## 阅读顺序

先读 `native/b_server.h` 了解服务器对外接口，再读 `native/loader.cpp` 的启动、停用循环。处理业务问题时进入下面对应模块，不需要从 JNI 映射表开始阅读。

| 文件 | 职责 |
| --- | --- |
| `native/loader.cpp` | 驻留控制线程、启停请求、DEL、失败清理重试 |
| `native/server/lifecycle.cpp` | 监听生命周期、A 连接获取、主线程等待门、登录超时 |
| `native/server/transport.cpp` | Netty 通道初始化、协议切换、native 回调和局域网公告 |
| `native/server/login.cpp` | B 登录、世界快照交接、玩家身份初始化 |
| `native/server/forwarding.cpp` | B 收包分发、实时转发、Bundle 展开、TAB 映射 |
| `native/server/teams.cpp` | 初始化和实时队伍包共用的 JNI 转换，保留队伍参数，不修改 A 的原包 |
| `native/team_state.h` | 记录发往 B 的队伍归属，映射 A/B 成员名，过滤重复或过期的退队操作；由 dispatchMutex 保护，每次登录重置 |
| `native/server/bindings.cpp` | 按协议、通道、玩家、缓冲区等分组解析 JNI 映射 |
| `native/server/bindings.h` | DLL 生命周期内保留的类引用和方法、字段 ID |
| `native/server/state.h` | 运行状态、连接、锁、等待条件、玩家身份 |
| `native/server/internal.h` | 仅供服务器内部文件调用的接口 |
| `native/world_snapshot.cpp` | 在游戏主线程读取世界并构建初始化数据包 |
| `native/packet_policy.h` | A/B 上行包的控制权与心跳归属规则 |
| `native/relay_handler.cpp` | A 的 Netty 入口及转发旁路标记 |
| `native/jni_lookup.cpp` | 类名读取及按描述符进行的兼容查找 |
| `native/mapped_method.h` | 已知 Mojang/SRG 名称的方法解析 |
| `native/snapshot_jni.h` | 快照使用的 JNI 调用、异常检查和局部引用帧 |
| `native/status_response.cpp` | 状态查询响应及 MOTD |
| `native/classfile.*`、`native/class_edit.*` | 类文件生成和编辑；不放服务器业务逻辑 |
| `native/connection_hook.cpp`、`native/trampolines.cpp` | JVM 与 native 回调连接层 |
| `injector/`、`native/jni_inject.cpp` | 启动入口和宿主侧调用接口 |

服务器外部只能依赖 `b_server.h`。不要从 `relay_handler.cpp` 或其他模块直接访问 `server` 全局状态；内部共享状态集中在 `proxy_server::server`，定义只在 `state.cpp` 中出现一次。

## 数据流

```text
A 的服务器 -> A Netty 回调 -> A 正常处理 -> B 转发
                                      -> 同步中暂存 -> 快照后补发
B 的操作   -> B 收包分发 -> 登录确认/控制权检查 -> A 的远端连接
```

主线程快照的边界依赖 A 的任务先入队：`relay_handler.cpp` 先交给 A 处理，再转发或暂存。调整顺序前必须检查快照与实时增量是否会重复或遗漏。

两种登录路径：

1. A 尚未连接服务器：让 A 的主线程等待 B 登录，然后释放主线程，用远端服务器的正常登录流初始化 B。
2. A 已在服务器：B 进入 `Syncing`，主线程生成快照，发送后进入 `AwaitReady`，收到本地初始化确认才进入 `Play`。

本地传送 ID 由 `LoginHandoff` 定义，只能由代理消费，不能发到远端服务器。超时、缓存满或生成失败时关闭 B，保留 A。

## 状态与线程

`ClientState` 描述 B 连接所处的协议阶段；`LoginHandoff` 专门管理中途接入的初始化确认和截止时间。它们服务于不同层次，不要把握手、状态查询和快照截止时间混入同一个开关。

| 状态 | 保护方式 |
| --- | --- |
| 快照交接、实时增量队列 | `dispatchMutex` |
| B 通道引用 | `clientMutex` |
| A 远端连接引用 | `targetMutex` |
| 接受的子通道弱引用 | `childrenMutex` |
| 等待 B 的条件变量、任务数量 | `gateMutex` |
| 生命周期状态开关 | 相应原子变量；不代表其他引用也无需锁 |

拿多个锁时，先拿 `dispatchMutex`，再拿连接锁。等待游戏主线程生成快照时不持有这些锁，否则游戏主线程可能无法进入快照边界。JNI 引用从共享状态取出后，先创建局部引用再释放连接锁，避免另一线程清理全局引用。

`RuntimeCallback` 保证停用时等待已进入的回调退出。停用顺序是通知等待门退出、停止接受新回调、排空旧回调，再关闭 B 和监听、撤销 Hook。清理失败必须允许重试，不能提前宣布已停用。

## JNI 引用

- `JavaBindings` 的类和对象全局引用由驻留 DLL 保留，暂停后复用。暂停不是 DLL 卸载。
- 连接、监听、玩家身份属于当前会话，停用成功时由 `lifecycle.cpp` 释放。
- 暂存数据包是全局引用，补发后或失败清理时删除。修改容器时检查异常分支是否同样释放。
- `CaptureWorldSnapshot` 返回的包归调用方所有，必须逐个删除全局引用。
- `LoadClassInLoader` 和 JNI 查找辅助函数返回局部引用；`GetMinecraftClassLoader` 返回全局引用。不要混用删除函数。
- 每次跨线程进入 JVM，都必须使用该线程自己的 `JNIEnv*`。禁止把 `JNIEnv*` 存入共享会话状态。

新增 Minecraft 映射时优先写明确的名称、SRG 名称和完整描述符。只有名字未知的兼容路径才使用按描述符枚举的方法；多个相同描述符的方法不能靠返回顺序区分含义。

## 改动落点

| 改动 | 首先修改 | 验证 |
| --- | --- | --- |
| 哪些上行包属于 A/B | `packet_policy.h` | `packet_policy` |
| 快照确认、截止时间 | `login_handoff.h`、`server/login.cpp` | `login_handoff` 与游戏重连 |
| 停用、恢复、并发排空 | `loader.cpp`、`server/lifecycle.cpp` | `runtime_gate` 与游戏 DEL |
| Minecraft 版本映射 | `server/bindings.cpp`、快照 JNI 调用 | `mapped_method`、原生 JNI 与目标版本运行 |
| 快照内容 | `world_snapshot.cpp` | 原生 JNI 与中途加入 |
| 状态查询 | `status_response.cpp` | 状态响应测试及服务器列表查询 |
| 类文件读写 | `classfile.*`、`class_edit.*` | `class_editor`、已有 `scripts/test.sh` JVM 验证 |

优先用普通函数和明确的状态字段表达流程。只有出现真实重复时再提取公共工具；不要通过包含 `.cpp`、宏拼接实现或通用事件总线重新把模块耦合起来。

## 构建与验证

日常运行 `./scripts/build.ps1`，它会编译并执行 CTest。`tests/CMakeLists.txt` 管理测试目标，根 CMake 只管理生产目标。`native/sources.txt` 是生产 DLL 的唯一源码清单；增加文件时同时更新清单，不依赖目录扫描自动加入文件。

```powershell
# 从新目录验证不依赖旧缓存。
./scripts/build.ps1 -BuildDir ./build/clean

# 使用本机 Minecraft 1.20.1 文件进行 JNI 与数据包编解码验证。
./scripts/test_snapshot_native.ps1 -JavaHome 'D:/Program Files/Java/jdk-21.0.12'
```

如果使用自定义构建目录，给原生 JNI 脚本传入 `-TestExe <构建目录>/tests/snapshot_jni_test.exe`。这项测试使用原版 SRG 类，不启动客户端，也不验证 Forge 的全部启动变换。

没有 JDK 的环境可以通过 `-WithoutJvmTests` 禁用已有 Java 测试夹具；生产逻辑始终为 C/C++，不会将这些测试夹具放进 DLL。

`.editorconfig` 定义编码和缩进，`.clang-format` 定义 C/C++ 格式。只格式化当前改动模块，避免把无关文件改动混入行为修复。

发布前仍需游戏内验证：A 入服后注入、A 入服前注入、B 断开重连、同步时断开、DEL 停用和再次启用。编译和单元测试通过不等于这些端到端流程已通过。磁盘 DLL 更新后必须重启 A 才加载新版。

## 范围

已删除的皮肤专用同步和旧 `world_cache` 不再维护。世界快照中的正常玩家列表和实时玩家信息仍属于入服协议。当前不支持把 A 本地模组提供的皮肤或自定义实体握手自动移植到 B。

`skid/` 是参考源码，`build/`、`logs/` 是本地生成内容，不参与业务模块拆分。`mindll/` 和 bare 构建保留为实验入口，变更其行为需要单独验证。
