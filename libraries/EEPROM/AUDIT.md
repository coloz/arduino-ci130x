# EEPROM 对照官方 SDK 审查

审查日期：2026-09-06。原实现基线：Git `25943df`。

结论：原库已经具备缓冲读写、显式提交、范围检查和 NVDM 接入，但在容量变化、失败恢复、内存所有权及初始化方面存在缺陷。本次修复继续使用官方 NVDM，不改动 Flash 分区或底层 SDK。

## 对照依据

- 本机独立官方目录：`D:/Git/chipintelli_arduino/CI130X_SDK_ALG_V2.7.14`。
- 仓库对应实现：[ci_nvdata_manage.c](../../tools/sdk/src/components/ci_nvdm/ci_nvdata_manage.c)、[ci_nvdata_port.c](../../tools/sdk/src/components/ci_nvdm/ci_nvdata_port.c)、[ci_flash_data_info.c](../../tools/sdk/src/components/flash_control/flash_control_src/ci_flash_data_info.c)。逐行对照确认下表中的短读校验、记录长度及状态写入返回值行为也存在于独立官方版本。
- `tools/sdk/UPSTREAM-SDK-README.md` 与该目录 README 的 SHA-256 相同：`1820DE4E755C71A00E7AC638707CF9EA2FE47692A8C2519A9076B4EC6FCB720F`。版本标识来自 SDK 目录名；该 README 本身没有版本号。
- 官方 [NVDM 使用说明](https://document.chipintelli.com/en/软件开发/SDK/CI130X芯片SDK/CI-SDK-Offline/CI130X_SDK_ASR_Offline_V2.2.0/API参考/存储API/nvdata/)用于交叉核对初始化、返回值检查和失败重试用法。在线页面对应不同 SDK 版本，具体判断以项目实际使用的源码为准。

## 缺陷与处理

| 等级 | 原问题及触发条件 | 依据 | 本次修复 |
| --- | --- | --- | --- |
| 高 | 保存较大记录后用较小的 `begin(size)` 重开，可能初始化失败 | SDK `cinv_item_read()` 在 1498、1502、1519 行按请求长度加载，但 1521 行按记录完整长度校验；未加载的尾部可能是内部缓冲残留 | 分配 240 B 缓冲，每次 SDK 读取始终请求 240 B，逻辑可访问范围独立管理 |
| 高 | 缩容后修改并提交会截断原记录尾部 | SDK `cinv_item_init()` 1380–1384 行不调整已有长度，`cinv_item_write()` 1457 行以新长度写入；旧库直接传逻辑 `_size` | 用 `_storageSize` 保留已保存长度与当前会话最大开放范围；缩容仍保留尾部；短记录不强制扩到 240 B |
| 高 | Flash 提交失败后 `end()` 仍释放缓冲；改变 `begin()` 大小时也会间接丢失未提交数据 | 旧 `end()` 未检查 `commit()` 结果，旧 `begin()` 调用 `end()` 后重新分配 | `end()` 返回 `bool`，失败保留打开状态和待保存数据；已打开实例的 `begin()` 只调整范围，不隐式提交或释放 |
| 高 | 拷贝对象后两个对象持有同一裸指针，产生悬空访问、重复释放 | 旧类拥有 `_data` 且有析构，未定义或禁止复制 | 删除复制构造及复制赋值；析构只尽力提交，并保证释放 |
| 中 | 初始化后读取返回 `CINV_ITEM_UNINIT` 仍被当作成功 | SDK 创建时 UNINIT 表示成功创建，但读取时表示不存在 | 初始化只接受 SUCCESS/UNINIT；读取仅接受 SUCCESS，并校验有效实际长度 |
| 中 | SDK 的 write 返回成功时，部分状态位可能未成功持久化 | SDK `write_item_status()` 返回布尔值，但 467、1453、1465 行等处忽略返回值 | 每次实际提交后完整回读并检查长度、内容；失败保持待提交状态，供调用者重试 |
| 中 | 中断上下文可能进入等待或 NVDM mutex；SDK 尚未启动时只能一直等到超时 | 旧库只等待 ready，没有主动启动 SDK 和 trap 检查 | `begin()` 主动调用 `chipintelli_sdk_begin()`；SDK FAILED 提前退出；begin/commit/关闭已打开实例拒绝 trap 上下文；保留 10 秒等待上限及计时回绕处理 |
| 低 | 相同值 `write()` 也标记脏，导致后续无效 Flash 更新 | 旧 `write()` 无条件赋值和置 dirty | write/update 同值不置脏，干净 commit 无 Flash I/O；示例对失败提交延时重试同一计数 |

SDK 枚举定义中的注释对 SUCCESS/FAILED 的文字存在错配，本次按函数实际实现及函数返回值说明判断，没有照搬枚举注释。

## 兼容性与成本

- 保留 NVDM ID `0x60454550`、默认逻辑容量 128 B、容量上限 240 B、擦除值 `0xff` 和现有 read/write/update/get/put/Bytes API。
- 旧短记录可以正常读入；实际提交只在需要时扩展到记录长度与本次会话最大开放范围的较大者。不会删除后重建记录，也不做全分区擦除。
- 每个打开实例固定使用 240 B 堆缓冲，增加一个长度字段；实际提交时使用 240 B 局部回读缓冲，并增加一次 SDK 读取。
- 仅扩容且没有字节修改时，commit 不写入；未保存的范围高水位只属于当前打开会话。
- `end()` 从 void 改为 bool，普通的 `EEPROM.end();` 仍可编译；若代码显式使用原 void 成员函数指针类型，需要同步调整。
- 改变打开实例的容量不再隐式提交。需要保存时应显式检查 commit/end 的返回值。

## 保留的边界

- 这是 EEPROM 风格的缓冲 API，没有 AVR 的下标代理或迭代器，不宣称完全兼容 AVR EEPROM。
- API 不支持并发访问；SDK mutex 不能保护 RAM 缓冲或完整的读改写事务。使用一个活动实例，并在任务间外部串行化操作；不得用原始 NVDM API 同时修改该 ID。
- 仅在正常任务调度且中断开启时使用。trap 检查不能代替对静态初始化、调度器挂起或临界区的使用约束。
- 析构无法报告失败，也不能让已经销毁的对象保留可重试缓冲，因此可靠保存应显式调用并检查 commit/end。
- 回读验证能发现可观察到的写入失败，但不证明任意掉电场景下的原子性或寿命。SDK 内部内存分配失败处理、损坏记录防御及状态位错误传播仍是底层独立课题。

## 验证

主机回归：使用已安装的 MSVC 14.50，`/W4 /WX` 严格编译并运行真实 EEPROM.cpp，18 组、97,626 次检查通过。SDK 声明来自仓库真实头文件；故障注入替代 NVDM 操作、时钟、上下文和分配器，不把主机结果当作真实 Flash 测试。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_eeprom.ps1
```

测试覆盖 1–240 B 全部容量、旧短记录/长记录的小窗口、尾部保留、实际写入长度、不变数据避免写入、整数范围和 typed API、分配失败、SDK 启动失败与超时/计时回绕、初始化/读取/提交/回读故障、失败后重试、trap 拦截、析构和分配边界守卫、禁止拷贝。

目标固件使用最终 PersistentCounter 示例、当前仓库核心、Nuclei GCC 9.2.0 和隔离 Arduino CLI sketchbook 完成编译、链接及完整后处理；四项均通过，未发现编译 warning/error。

| 配置 | Flash sections / B | RAM / B | 双核 user_code / B | 完整固件 / B |
| --- | ---: | ---: | ---: | ---: |
| CI1302 / null | 135549 | 120908 | 211456 | 2056225 |
| CI1303 / null | 135549 | 120908 | 211456 | 2056225 |
| CI1306 / null | 132111 | 121048 | 208016 | 2052129 |
| CI1306 / aec | 132167 | 121056 | 208520 | 2052129 |

以上是整个示例固件的资源用量，不是 EEPROM 单库占用。所有配置保留 16 KiB NVDM 分区，CI1302 起址为 `0x1FC000`，CI1303/CI1306 为 `0x3FC000`。逐配置核对源码 SHA-256 及 EEPROM 目标文件生成时间，确认构建使用最终实现。

本次构建运行脚本、源文件快照、日志、固件及 `validation.json` 位于忽略目录 `.build/eeprom-target/`，主机测试产物位于 `.build/eeprom/`。本机可使用下列命令重跑目标构建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .build/eeprom-target/run-builds.ps1
```

该目标构建脚本记录的是本机工具路径，不作为跨机器通用测试入口。永久保留的主机测试入口为 [tools/test_eeprom.ps1](../../tools/test_eeprom.ps1)。

`git diff --check` 对本次 EEPROM 变更通过。实体板掉电、磨损和跨复位测试未在本次执行，也未烧录硬件。
