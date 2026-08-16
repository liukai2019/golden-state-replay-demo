# GSR/2 bundle protocol

GSR/2 是 UTF-8、可 diff 的选择性状态重放格式。首版 bundle 只有两个必需文件：

```text
scenario/
  manifest.json
  state.jsonl
```

`schema.json` 不是首版运行时依赖。类型布局由 Clang 生成的强类型 C++ codec 掌握；
manifest 只记录 `build_id`、`type_fingerprint`、pointer width 等兼容性护栏。

## `manifest.json`

必需字段：

| Field | Meaning |
|---|---|
| `format`, `version` | 固定为 `golden-state-replay`, `2` |
| `scenario` | 人可读场景名 |
| `target` | 该 bundle 对应的 generated codec/函数入口 |
| `capture_point` | 例如 `function_entry` |
| `build_id` | 产生 fixture 的构建标识 |
| `type_fingerprint` | 所有参与类型、字段和布局的稳定摘要 |
| `pointer_size`, `endianness` | ABI 基本条件 |
| `data_classification` | `synthetic`、`tokenized` 等脱敏等级 |

loader 必须先校验 manifest，再分配任何对象。

## `state.jsonl`

每行是一个独立、扁平 JSON object。扁平设计使最小 C++ demo 不依赖第三方 JSON 库；
生产版可以使用成熟 JSON/CBOR 实现，但记录语义保持不变。

### 对象和值

```json
{"kind":"object","id":"sip-1","type":"SipRuntime"}
{"kind":"scalar","object":"sip-1","field":"config_epoch","encoding":"i32","value":7}
{"kind":"string","object":"sip-1","field":"service.public_user_id","value":"sip:user-001@example.test"}
```

object ID 只在当前 bundle 内稳定，从不保存捕获进程的裸地址。字段由 generated codec
按真实 C++ 类型读取和写入；未知类型、字段或编码必须失败。

### 普通指针与子对象指针

```json
{"kind":"edge","from":"sip-1","field":"primary_ua","to":"ua-1"}
{"kind":"edge","from":"ua-1","field":"dialogs.head","to":"dialog-1","subobject":"link"}
```

第二条表示：

```cpp
ua->dialogs.head = &dialog->link;
```

即使 `link` 当前恰好位于 offset 0，也必须保留 `subobject` 语义。这样 loader 不会把
`ListEntry*` 与 `Dialog*` 混为一谈，未来布局变化时也会由 type fingerprint 拒绝旧数据。

空指针写为 JSON `null`：

```json
{"kind":"edge","from":"dialog-1","field":"link.next","to":null}
```

### 参数根、全局根和普通值

```json
{"kind":"root","name":"arg.sip_ptr","to":"sip-1"}
{"kind":"root","name":"arg.service_ptr","to":"sip-1","subobject":"service"}
{"kind":"root","name":"arg.h_ua","to":"ua-1"}
{"kind":"value","name":"arg.event","encoding":"i32","value":200}
{"kind":"root","name":"global.g_runtime","to":"sip-1"}
{"kind":"global","name":"g_feature_enabled","encoding":"bool","value":true}
```

这些记录可以同时表达：

```cpp
service_ptr == &sip_ptr->service;
g_runtime == sip_ptr;
ua_event_ptr->header.ua == h_ua;
```

### 可选的 oracle 与 spy transcript

```json
{"kind":"oracle_string","root":"arg.event_ptr","field":"call.to","value":"sip:peer-001@example.test"}
{"kind":"expected_call","name":"notify_dialog_confirmed","argument":42}
```

若项目已经把预期明确写在 GTest 中，可以完全不输出这两类记录。保留它们适合直接把
集成测试观察到的出口值作为 golden oracle。

## Loader 顺序

1. 解析并校验 manifest、大小限制和 type fingerprint。
2. 为完整对象分配零初始化存储；内嵌子对象不得重复分配。
3. 写入 scalar、enum、bool、有界字符串和数组字段。
4. 修复 pointer edge、alias、环、`void*` handle 和 intrusive-list subobject。
5. 绑定参数根与选定全局根，运行结构不变量。
6. 安装现有 stub/spy 后调用真实函数，最后比较选择性 oracle。

先分配后 fixup 是处理 forward reference、别名和环的关键。任何地址若无法解析成登记
对象或合法子对象，都必须由 policy 明确设为 adapter/stub，否则拒绝 capture/replay。

## Capture policy

编译数据库只能提供“有哪些全局变量和类型”，不能决定“哪些与这个分支相关”。实际
roots 应是 Clang inventory 与人工 allowlist 的交集。每条指针边必须落入一种策略：

| Strategy | Meaning |
|---|---|
| `traverse` | 目标是模块拥有、受深度/对象数/字节预算约束的对象 |
| `embedded` | 指向另一对象的结构体/数组子对象 |
| `borrowed` | 只在目标已由其他拥有路径捕获时建立 edge |
| `adapter` | 根据稳定 token 重建 timer/lock 等测试替身 |
| `stub` | OS/net/log/其他模块由现有测试 stub 接管 |
| `ignore` | 已证明与 action/oracle 无关的缓存、统计、调试状态 |

未知指针默认拒绝，不能从少数全局根无界遍历整个进程。

## 安全与兼容边界

- capture 只保存 allocation registry 中的地址；拒绝越界、one-past-end 和未登记指针。
- loader 不恢复原始 socket/线程/锁/TLS/vptr/函数地址或内核对象。
- 函数指针若必须使用，只能按 allowlist 中的符号标识绑定。
- parser 在分配前限制文件大小、record 数、对象数、字符串长度和总分配字节。
- 跨线程状态应在业务静止点捕获；只暂停一个线程不足以防止撕裂对象图。
- URI、号码、账号和服务器地址应在导出端 tokenization；不要依赖提交前人工搜索。
- ABI/type fingerprint 不匹配默认拒绝；显式 migration 工具负责转换旧 fixture。
