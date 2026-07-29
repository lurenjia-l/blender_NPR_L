# NPR Bridge 节点设计规格

- 日期：2026-07-21
- 状态：草案，待人类伙伴复核
- 决策：BSDF 桥接走路线 A 起步

## 1. 背景与目标

### 1.1 现状痛点

- **AOV Output（材质树）/ AOV Input（NPR 树）只支持 Color + Value（float）**：因为 Eevee AOV render pass 系统只有 RGBA + float 两类（`eevee_film.cc`）。
- **AOV 无法接 BSDF**：socket 类型不匹配（AOV Output 只有 Color/Value 输入，无 Shader 输入）。
- **AOV 两边各手动命名一次**：靠 `BLI_hash_string(name)` 匹配，繁琐易错。
- **Shader to RGB 能接 BSDF 但丢信息**：设 `GPU_MATFLAG_SHADER_TO_RGBA` 强制材质树内求值 closure，丢失 deferred indirect / probe / Another BSDF mix 后的完整光照。
- **NPR Input 节点固定 9 输出**（Combined/Diffuse/Specular 的 color/direct/indirect + Position/Normal），无法传材质树自定义数据。

### 1.2 目标

造一对桥接节点 **NPR Bridge Output（材质树）/ NPR Bridge Input（NPR 树）**：

- Color / Float / Vector 数据从材质树传到 NPR 树，支持二次颜色 / 数学 / 矢量运算。
- BSDF 数据：路线 A——桥接 BSDF 与主输出同链时，NPR 树拿到完全光照结果（== BSDF 直连 Material Output 的画面，含 Mix Shader 在内的最终光照）。
- 命名：自动生成（材质名 + 顺位）或手动指定；NPR Bridge Input 下拉选已注册 name（像 Texture Coordinate 选 object）。
- 独立节点对，不塞进现有 NPR Input。

### 1.3 非目标（路线 A 下）

- 不支持"独立 BSDF 链"（不连主输出）的完全光照结果——需路线 C，后续升级。
- 不拆 BSDF 各参数（diffuse / specular / roughness），只取 color。
- 不改变现有 NPR Input / AOV 节点行为。

## 2. 路线 A 决策摘要

BSDF 在材质树里是 closure（参数集），非颜色。"完全光照结果"必须经 deferred lighting。每个材质的 deferred lighting 只评估一条 closure 链（Material Output Surface 那条）。

路线 A 行为：

- 桥接 BSDF 链 == 主输出链（最常见：Mix Shader → 同时连 Material Output + Bridge Output）→ NPR Bridge Input 的 Shader 输出返回 `g_combined_color`（该像素完整光照结果，语义 == NPR Input 的 Combined Color）。零额外光照开销。
- 桥接 BSDF 链 != 主输出链 → 回退（见 §5.3）。

## 3. 节点设计

### 3.1 NPR Bridge Output（材质树）

- 注册：`SH_NODE_NPR_BRIDGE_OUTPUT`，poll = eevee shader（普通物体材质）。
- Socket（多输入，每个独立桥接一条数据）：
  - `Color`（`decl::Color`）
  - `Float`（`decl::Float`）
  - `Vector`（`decl::Vector`）
  - `Shader`（`decl::Shader`）——接收 BSDF / Mix Shader 输出
- 存储：`NodeShaderNPRBridge { char name[64]; }`（name 可空，空则自动生成）。
- UI：name 文本框（可空）+ 顺位编号只读显示。
- 自动命名：name 空 → `<material_name>_<node_index>`（如 `Skin_0`）。

### 3.2 NPR Bridge Input（NPR 树）

- 注册：`SH_NODE_NPR_BRIDGE_INPUT`，poll = npr shader。
- Socket（多输出，对应 Output 的输入）：
  - `Color`（`decl::Image`，图像句柄，支持 `Image Sample` 邻域采样）
  - `Float`（`decl::Image`）
  - `Vector`（`decl::Image`）
  - `Shader`（`decl::Image` 或 `decl::Color`）——返回光照结果颜色
- 存储：同 `NodeShaderNPRBridge`，name 字段。
- UI：name 下拉（枚举当前材质树所有 NPR Bridge Output 注册的 name）+ 手动输入，类似 Texture Coordinate 选 object。
- 匹配：`hash(name)`，复用 AOV 的 hash 机制。

## 4. 数据通路设计

### 4.1 Color / Float / Vector（需 buffer 传递）

材质树与 NPR 树是两次独立编译（`ntreeGPUNPRNodes` 把 NPR 树 inline 成 localtree 执行），数据需中间存储。

- 材质树编译时，NPR Bridge Output 求值各输入，写入 bridge render pass buffer（复用 / 扩展 AOV 的 `rp_color_img` / `rp_value_img` 机制）：
  - `Color` → RGBA 通道
  - `Float` → R 通道（复用 value buffer）
  - `Vector` → XYZ 存 RGB 通道（A=1）或单独 vector buffer（见 §7.2 待确认）
- NPR 树编译时，NPR Bridge Input 按 hash 查 bridge buffer，生成 `TextureHandle` 读取（复用 `TextureHandle_eval_impl` 机制，新增 `TEX_HANDLE_BRIDGE_*`）。
- name 全局唯一（材质名前缀保证），buffer 为 view layer 级（同 AOV）。

### 4.2 BSDF / Shader（无需 buffer，直接读 g_combined_color）

- NPR Bridge Output 的 Shader 输入：编译期静态分析该 closure 链是否 == Material Output Surface 的 closure 链（closure 链溯源）。
  - 同链：NPR Bridge Input 的 Shader 输出生成读 `g_combined_color` 的代码（== `TEX_HANDLE_COMBINED_COLOR` 语义）。
  - 不同链：见 §5.3 回退。
- 数据不经过 buffer：BSDF 桥接的"数据"就是该像素光照结果，NPR 树执行时 `g_combined_color` 已就绪（NPR Pass 在 deferred lighting 之后，见 `eevee_surf_deferred_npr_frag.glsl` 的 `main()`）。

## 5. 关键行为

### 5.1 同链检测（BSDF）

- 编译期：从 NPR Bridge Output 的 Shader 输入向上溯源 closure 链，与 Material Output Surface 的 closure 链比对。
- 判定同链：两条链汇聚到同一 closure 节点（同一 Mix Shader 输出，或同一 BSDF 节点）。
- 参考工具：`node_shader_tree.cc` 已有 `node_chain_iterator_backwards` 等链遍历基础设施。

### 5.2 Mix Shader 处理（用户 q1）

Mix Shader 输出是 mix 后 closure。当 Mix Shader → Material Output + Bridge Output 时，同链检测成立，Bridge Input 返回 `g_combined_color`（含 Another BSDF 在内的完整光照）。满足用户诉求："mix shader 连到 NPR Bridge Output 的颜色 = mix shader 最终画面"。

### 5.3 不同链回退（路线 A 的边界）

桥接 BSDF 不连主输出（独立链）时，路线 A 无法拿到其完全光照结果（需路线 C）。回退策略待确认（见 §7.1）。

### 5.4 命名（用户 q2）

- NPR Bridge Output：name 可空，空则 `<material_name>_<node_index>`。
- NPR Bridge Input：name 下拉枚举当前材质树 NPR Bridge Output 的 name，也可手输。
- 跨材质：name 全局唯一（材质名前缀），不同材质的 NPR 树理论上能读到其他材质的 bridge（灵活性，但默认同材质使用）。

## 6. 与现有系统的关系

- 不修改 NPR Input 节点（保持 9 输出不变）。
- 不修改 AOV Output / Input（保持兼容）。
- 复用 AOV 的 hash 匹配 + render pass buffer 基础设施（扩展 vector 类型 + bridge handle）。
- 复用 `TextureHandle_eval_impl` 机制（新增 bridge handle type）。
- NPR 树 inline 执行路径（`ntreeGPUNPRNodes`）已支持新节点类型（按 type 注册即可）。

## 7. 待确认设计点

1. **不同链回退策略**（§5.3）：
   - (a) 编译警告 + Bridge Input 的 Shader 输出返回黑色或 `g_combined_color`（不断图但语义不准）
   - (b) 自动降级为 Shader to RGB（丢信息，违背初衷，不建议）
   - (c) 编译期拒绝（节点报错，要求用户改同链）
2. **Vector buffer 方案**：复用 color buffer 的 RGB 通道 / 新增 vector buffer？（影响工程量）
3. **BSDF 同链检测严格度**：只判"同一 closure 节点" / 还是允许"等价 closure 参数"？（后者复杂，建议前者）
4. **跨材质 bridge 可见性**：默认同材质闭环 / 允许跨材质读？（影响命名和 buffer 作用域）

## 8. 实现计划概要（获批后拆分细任务）

### 阶段 1：Color / Float / Vector 桥接（无 BSDF）

- T1：注册 `SH_NODE_NPR_BRIDGE_OUTPUT` / `SH_NODE_NPR_BRIDGE_INPUT` 节点类型 + storage
- T2：NPR Bridge Output 的 Color / Float / Vector 通路（hash + buffer 写入）
- T3：NPR Bridge Input 的 Color / Float / Vector 通路（`TextureHandle` + GLSL 读取）
- T4：命名机制（自动生成 + 下拉枚举 UI）
- T5：测试（材质树写、NPR 树读、邻域采样）

### 阶段 2：BSDF 桥接（路线 A）

- T6：同链检测（closure 链溯源）
- T7：BSDF 通路（同链读 `g_combined_color`，不同链回退）
- T8：测试（Mix Shader 同链、独立链回退）

## 9. 自审记录（占位符 / 一致性 / 范围 / 歧义）

- 占位符：无（实现细节留到任务拆分）。
- 一致性：§4.2 声称 BSDF 无需 buffer，与 §4.1 的 buffer 机制不冲突（两类数据走不同通路，已在节点 socket 设计中区分）。
- 范围：路线 A 明确排除独立 BSDF 链的完全光照（§1.3），回退策略集中在 §5.3 / §7.1。
- 歧义：§7 四个待确认点是主要歧义源，需人类伙伴复核定夺。
