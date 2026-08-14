# GSR/1 text protocol

GSR/1 是行式、UTF-8、可 diff 的 selective state replay 协议。空行和以 `#`
开头的行被忽略。第一条有效记录必须是 `GSR/1`。字符串使用 C++
`std::quoted` 兼容的双引号与转义。

## 记录类型

| Record | Meaning | Demo example |
|---|---|---|
| `meta key "value"` | 构建、场景、捕获点或 schema 元数据 | `meta scenario "connected_call"` |
| `object id type count` | 一次拥有 `count` 个同型元素的堆分配 | `object legs MediaLeg 2` |
| `external id adapter token` | 由 adapter 从稳定 token 重建的资源 | `external refresh_timer TimerHandle 9001` |
| `global name encoding value` | 非指针全局变量 | `global g_service_ready bool true` |
| `scalar id i field encoding value` | 对象的标量/枚举字段 | `scalar call 0 state enum 1` |
| `string id i field "value"` | 有界文本字段 | `string peer 0 uri "sip:..."` |
| `edge owner i field target j` | 指针字段或全局指针根 | `edge call 0 active_leg legs 1` |
| `expect_call name argument` | 回放期望的外部调用 | `expect_call stop_timer 9001` |
| `expect id i field encoding value` | action 后选择性 oracle | `expect call 0 state enum 2` |

`null 0` 是保留的空指针目标。非零 null index、未知 object ID、越界 element
index、字段/目标类型不匹配以及未知 adapter 都必须使加载失败，不能静默修复。

## 指针语义

协议从不保存原始地址。每个可序列化分配有稳定 object ID；指针写成：

```text
edge <owner-object-id> <owner-element-index> <field-id>
     <target-object-id> <target-element-index>
```

因此多个字段指向同一 `(id,index)` 时仍是别名；A→B→A 可表达环；数组中第
`j` 个元素是 interior pointer。生产版应把 element index 扩展为经过验证的
`byte_offset + pointee_type`，以覆盖柔性数组、子对象和 `char*` view，同时拒绝
不满足对齐与边界约束的 offset。

## 加载顺序

1. 校验 header、schema/ABI fingerprint、limits 与所有 record。
2. 为所有 `object` 分配零初始化存储，并用 adapter 创建 `external`。
3. 写入 scalar/string/global value 字段。
4. 解析全部 edge，修复指针并绑定全局 roots。
5. 运行 fixture invariant；随后才允许调用被测 action。
6. fake/stub 校验 transcript，oracle 只比较声明的重要输出。

先分配后 fixup 是处理 forward reference、别名与环的关键。构造外部资源必须走
adapter；不得对捕获地址做 `reinterpret_cast`。

## 建议的生产 bundle

Demo 为方便阅读把记录放在一个文件中。大规模场景建议保持相同语义并拆分：

```text
scenario.gsr/
  manifest.json       # product/build/ABI/schema/capture-point/checksums
  schema.json         # Clang 生成的 type-id, size, align, field offset/type
  state.jsonl         # objects, values, edges and roots
  calls.jsonl         # ordered/unordered external interaction transcript
  oracle.json         # selected post-action observations
```

生产 manifest 至少应包含 target triple、endianness、pointer width、compiler、
build ID、schema hash、roots policy hash、fixture size limits 和数据脱敏等级。

## Capture boundary 与裁剪策略

全局变量 inventory 来自 `compile_commands.json` 与 Clang AST，但 capture roots
必须由 allowlist 决定。每条 pointer edge 应匹配下列策略之一：

| Policy | Use |
|---|---|
| `owned` | 跟随到 allocation registry 中的对象，并受深度/字节预算限制 |
| `borrowed` | 只在目标已被其他 owned 路径捕获时建立 edge |
| `external(adapter)` | timer/socket/handle 等由稳定 key 重建 |
| `stub` | 距目标模块过远的平台依赖，回放调用 transcript |
| `ignore` | 证明与 action/oracle 无关的缓存、统计或调试状态 |

未知 pointer 默认应失败或 stub，不能无界遍历整个进程。捕获必须在业务定义的
quiescent point 进行；仅暂停单个线程不足以保证跨线程对象图一致。

## Compatibility and security

- 同一 schema 内字段顺序不具有语义；object ID 只在单个 fixture 内唯一。
- ABI/schema 不匹配默认拒绝，显式 migration 工具负责转换旧 fixture。
- 解析前限制 record 数、总字节、allocation 大小、图深度和字符串长度。
- fixture 是不可信输入；loader 不调用任意函数指针，不恢复 vptr、锁内部字节、
  TLS 或内核对象。
- 需要捕获敏感号码/URI 时在导出端 tokenization；不要依赖提交前人工清理。
