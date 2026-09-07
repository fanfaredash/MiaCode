---
lifecycle: working
owner: architecture/tooling
last_verified: 2026-09-06
---

# MiaCode 规格与指引技能治理重整设计

> 状态：已按 2026-09-06 用户授权实施精简方案；原始详细设计保留作背景，以下实施取舍优先。
>
> 日期：2026-09-05
>
> 基线：`feature/qml-ui`，`HEAD 7202e5fd`

## 实施取舍（2026-09-06）

用户明确允许按实际判断调整本设计，并要求 skill 简单、避免频繁更新。本次采用以下落地边界：

- 保留全部现有 Spec 的源码、target、链接依赖、CTest 名称与独立进程；按域迁移注册，增加轻量分类、唯一性/漏登记校验和生成目录。CTest 枚举顺序随域分组变化，按名称和标签定向执行。
- 不为了减少 executable 删除或合并断言；没有实际退役或 bundle 时，不提前实现 history、waiver、case runner 和性能基准框架。
- 现有 `playback_coordinator_construction_spec` 实际是 `EXCLUDE_FROM_ALL` 的未闭合链接实验；如实登记为 `compile-only / blocked-link`，不把“有目标”当成通过编译验证。
- skill 采用短入口加三份参考（仓库与复用地图、跨模块同步、开发与验证）；删除逐函数、参数、flag、硬编码值的平行清单。`.agents` 唯一维护源，两个客户端生成同内容镜像。
- 文档 current/verification 才要求稳定 ID 与可验证引用；working/archive 不补造复核日期。代码锚点用文件/目录，不维护行号；索引扫描公开文件，遵循 Git 忽略规则保护本地记录。
- 显式过时方案标历史；混有待复核领域规则的材料标 working。当前应用/渲染规范独立编写；原 backend 清册重写为 Session 边界并保持现有 Spec 的格式兼容。
- 生成文件由各自工具检查，不增加豁免登记框架。原设计中的全量元数据、跨平台 skip wrapper、束化试点与三轮 clean build 门槛不作为本次验收要求。

维护规则见 [文档入口](../../README.md) 与 [贡献指南](../../../CONTRIBUTING.md)，
验证及已知基线问题见 [实施结果](../../audit/SPEC_AND_GUIDE_GOVERNANCE_RESULT_ZH.md)。
本节之后保留的是原设计，不是另一份实施规范。

## 1. 目标与范围

MiaCode 当前有两类容易混淆的“规格”：

1. `src/tools/**/*Spec.cpp` 中的可执行契约规格。当前共有 111 个，其中 110 个注册为
   CTest，`PlaybackCoordinatorConstructionSpec` 是未注册 CTest 的 compile-only 规格。
2. `docs/specs/`、`docs/superpowers/specs/` 和 `docs/audit/` 中的规范、研究、执行计划和
   历史记录。

本设计处理以下问题：

- 为每个可执行规格建立状态、类型、owner 和 canonical contract；
- 识别失效、重复、诊断性质和应继续保留的规格；
- 在不削弱编译/链接/进程隔离的前提下，合并同一契约下的小型策略规格；
- 将 CMake/CTest 规格注册变成可校验的 domain manifest；
- 将 MiaCode 开发指引重建为单一维护源，并为不同客户端生成兼容镜像；
- 将当前文档、验证清单、历史归档和工作记录分开。

本设计不改变产品代码、运行时行为、渲染路径、音频协议或导出协议，也不预设必须把
111 个目标压缩到某个固定数量。

## 2. 当前证据与治理诊断

### 2.1 可执行规格

规格主要集中在：

| 目录 | 数量 | 主要性质 |
| --- | ---: | --- |
| `src/tools/preview` | 30 | 渲染、音频、缓存、设备变更、线程和平台策略 |
| `src/tools/v2` | 22 | `ChartWorkspace`、ApplicationServices、Port/Host、Playback 边界 |
| `src/tools/qml_ui` | 19 | QML 投影、生命周期、页面和源码契约 |
| `src/tools/debug_index` | 8 | debug flag、日志、诊断和复现脚本 |
| `src/tools/video_export` | 5 | 导出 runtime、音频、片头和媒体时间线 |
| 其他目录 | 27 | parser、timeline、editor、Muri、cover export 等 |

现有 [CMakeLists.txt](/Users/caoyusen/Desktop/MiaCode/CMakeLists.txt:1190) 已通过
`miacode_add_dev_tool()` 消除部分构建样板，但还没有统一的规格生命周期和契约索引。

### 2.2 指引技能

当前存在三套同名技能：

- `.agents/skills/miacode-dev-guide/`
- `.claude/skills/miacode-dev-guide/`
- `.codex/skills/miacode-dev-guide/`

三套内容已经漂移。部分文件仍引用已经删除的 `src/app/mainwindow/`、旧 Widgets 外壳或
DComp 路径；另一方面，当前 [src/app/runtime/ASSEMBLY.md](/Users/caoyusen/Desktop/MiaCode/src/app/runtime/ASSEMBLY.md:1)
已经把 `Session + Runtime Hosts + ApplicationServices` 作为现行装配模型。

现有 [CONTRIBUTING.md](/Users/caoyusen/Desktop/MiaCode/CONTRIBUTING.md:17) 声明 `.codex`
是维护入口，但该目录并不是内容最完整或最近更新的一份。因此需要先确认唯一维护源，
再修正贡献指引和兼容目录。

### 2.3 文档生命周期

现有 [docs/README.md](/Users/caoyusen/Desktop/MiaCode/docs/README.md:1) 已经区分公开规格、
测试清单和历史归档，但 UI v2、封面导出和旧渲染方案仍同时存在规范、研究、计划和历史
资料，需要机器可识别的生命周期和 canonical ID。

## 3. 不变量

治理工作必须保持以下不变量：

1. 代码是最终事实来源；文档和技能不能覆盖代码事实。
2. 每个当前契约有且只有一个 canonical owner；该 owner 可以由一个独立规格或一个明确
   的 bundle 覆盖，不能存在未说明的重复入口。
3. 每个 live `*Spec.cpp` 都能说明自己保护的契约、owner、CTest 状态和独立隔离理由；已退役
   源文件由 history ledger 保留同样的审计信息。
4. `ChartWorkspace` 是 UI v2 的文档所有者；ApplicationServices typed slots 和 Runtime
   Hosts 是当前边界。
5. parser、timeline、preview、audio、export 和 Muri 的同步关系不能因规格合并而消失。
6. preview/audio worker、QML engine、平台设备和导出 worker 的隔离不能因减少目标而削弱。
7. 删除或合并必须有逐断言覆盖映射和可回滚的独立提交。

## 4. 可执行规格模型

### 4.1 分类字段

每个规格在 registry 中至少拥有以下字段：

| 字段 | 可选值或含义 |
| --- | --- |
| `entry_type` | `spec`、`bundle`、`case`、`history`；分别表示独立规格、bundle owner、bundle case 和历史账本行 |
| `disposition` | `active`、`legacy`、`duplicate`、`diagnostic` |
| `kind` | `pure-policy`、`behavioral-contract`、`static-guard`、`integration` |
| `domain` | `preview`、`audio`、`v2`、`qml_ui`、`timeline`、`export` 等 |
| `source` | 原始 `*Spec.cpp` 路径；live `spec/case` 必填，bundle owner 填 runner 源路径，history 可保留已删除路径文本 |
| `target` | executable target；独立规格或 bundle owner 必填，bundle case 记录不重复声明 |
| `ctest_name` | CTest 名称；`ctest/platform-ctest` 必填，bundle case 可各自拥有一个定向入口 |
| `link_deps` | target 的显式链接闭包；bundle owner 与独立规格必填 |
| `platform` | `all` 或平台集合 |
| `platform_gate` | `none` 或可审计的平台门控表达式；`platform-ctest` 必填 |
| `risk` | `high`、`normal` |
| `execution` | `ctest`、`platform-ctest`、`manual`、`compile-only` |
| `owner` | 对应生产模块或 host |
| `spec_id` | 每个逻辑规格的唯一 ID |
| `contract_id` | 稳定契约名，不依赖易变文件名；同一 bundle 内的多个 case 可以共享 |
| `bundle_id` | 合并后共享 Runner 的逻辑规格组；未合并时为空 |
| `case_name` | bundle 内逻辑 case 的唯一名称；未合并时为空 |
| `canonical` | 重复规格指向的唯一保留规格 |
| `superseded_by` | legacy/duplicate 记录指向的替代规格或 bundle |
| `retirement_reason` | legacy 没有替代者时的退役原因；duplicate 不使用 |
| `risk_waiver` | 高风险规格需要 bundle 或降级时的审批、证据和回滚引用 |

记录关系固定如下：独立规格使用一行 `entry_type=spec`，同时拥有自己的 `source` 和
`target`；bundle 使用一行 `entry_type=bundle` 作为 owner，`bundle_id` 等于该 owner 的
`spec_id`，拥有 runner `source`、唯一 `target` 和 `link_deps`；每个原始 `*Spec.cpp` 使用
一行 `entry_type=case`，以 `bundle_id + case_name` 关联 bundle，保留自己的 `source`、
`contract_id` 和验证入口，但不声明 `target`。`canonical` 和 `superseded_by` 只能引用
live 的 `spec_id`，或引用 live bundle 的 `bundle_id + case_name`；`entry_type=history` 只
进入审计账本，不进入构建。这样既能校验 111 个原始源文件的归属，也能表达多个 case 共享
一个 executable。

### 4.2 处理规则

#### Active

规格映射到当前生产代码，并能确定性地通过或失败。平台专属但确定性的边界守卫仍属于
active，而不是 diagnostic。

#### Legacy

仅保护已经删除的 v1/Widgets 产品路径，或断言与当前 v2 架构冲突，并且不存在继续防止
回归的价值。删除源码和 live build 注册；历史决策保留在 audit catalog、Git 或简短的
archive 记录中，不长期保留“编译但不运行”的死规格。

#### Diagnostic

依赖真实硬件、外部语料库、性能波动、人工解释或现场观察。保留为 probe/tool，继续受
`MIACODE_BUILD_DEV_TOOLS` 控制，但不注册为默认 CTest。

#### Duplicate

另一规格已经在相同或更合适的边界完整覆盖其断言。必须保留逐断言映射，不能仅因为名称
相似、目录相同或共用生产源文件就判定重复。

### 4.3 合并规则

只有同时满足以下条件，才允许合并为 bundle：

- 相同生产 owner 和 `contract_id`；
- 多个逻辑 case 若共享 `contract_id`，必须显式拥有同一个 `bundle_id`；
- 相同链接闭包、平台门控和 Qt application 生命周期；
- 不共享会互相污染的进程级全局状态、音频设备、QML engine 或环境变量；
- 合并后仍可按 `--case <case-name>` 或独立的 CTest 入口定向执行；
- 一个 case 失败不会掩盖其他 case，失败输出仍能定位到原契约。

`risk=high` 默认不得进入 bundle，且不得仅为减少 executable 而从 `active` 降为
`diagnostic`、`duplicate` 或 `legacy`。本次迁移中只要发生 bundle 或上述降级，registry 必须附带结构化
`risk_waiver`：`approver`、`approved_on`、`reason`、逐断言 `evidence` 和
`rollback_ref`；治理检查验证这些字段非空且引用可定位。没有 waiver 的高风险记录只能
保持独立的 active/diagnostic 形态；基线中已经是 legacy 的高风险记录可以凭
`retirement_reason` 保留为历史记录，但不得被计入合并收益。

bundle 本身拥有唯一的 executable target；bundle 内的 case 记录只声明 `bundle_id` 和
`case_name`，不重复声明 target。未合并的规格直接拥有自己的 target。这样 target 的唯一性
与 case 的可共享性不会冲突。

不因目录相同而自动合并。

### 4.4 初步保留与合并候选

以下是第一轮审计的候选方向，不是未经逐断言核对的删除清单。

应继续保持独立的典型规格：

- `ChartWorkspace`、`ApplicationServices`、Port/Host 和 Playback 边界规格；
- `RuntimeContextBoundarySpec`、`PlaybackStorageBoundarySpec`；
- `DependencyAllowlistSpec`、`DebugFlagIndexSpec`；
- QML engine/lifecycle 规格和 `QmlEditorControllerSpec`；
- `PreviewAudioWorkerSpec`、设备变更、BASS lease、non-GUI barrier；
- `TimelineModelSpec`、`ChartBatchTransformSpec`、`SimaiParserSpec`、`MuriSpec`；
- QtAVPlayer、导出 runtime 和其他平台/线程/进程隔离规格。

可先评估为小型 bundle 的典型集合：

- timeline 的 `TimelineMarkerOffset`、`TimelineCadenceArbitrationPolicy`、
  `TimelineQuickTextureCachePolicy`；
- preview/audio 中没有设备或 worker 生命周期的纯策略规格；
- QML 源码静态契约，如导出字体、片头音效、封面导出、文档生命周期和主菜单；
- debug_index 中不依赖窗口、进程或现场环境的纯策略规格。

第一阶段可以只共享测试辅助函数和 registry，不立即合并源码。只有测量到构建或维护
收益后，才进行一个小范围 bundle 试点。

## 5. CMake/CTest 注册表

在保留现有 `miacode_add_dev_tool()` 的基础上，引入规格语义层：

```text
cmake/devtools/
  MiaCodeSpecRegistry.cmake
  specs/
    index.cmake
    preview.cmake
    qml_ui.cmake
    v2.cmake
    timeline.cmake
    debug_index.cmake
```

建议增加 `miacode_add_spec()` 和 `miacode_finalize_spec_registry()`：

- domain manifest 显式列出每个 source 和链接闭包；
- 文件系统中的 `src/tools/**/*Spec.cpp` 必须在 live `spec` 或 `case` 记录中恰好注册一次；
- `bundle` owner 负责 runner target，`case` 记录负责原始断言、`case_name` 和 CTest 定向入口；
- active 规格默认必须有验证入口；`execution=ctest` 才注册 CTest，`execution=compile-only`
  只要求目标成功编译；
- diagnostic 不得意外注册为默认 CTest；
- live `spec/case/bundle` 的 `spec_id` 和原始 source 不得重复；live executable/bundle owner
  的 target 不得重复，bundle case 不声明 target；同一 bundle 内的 `contract_id` 可以由多个
  case 共享，除此之外 live canonical owner 不得重复，且 `bundle_id + case_name` 必须唯一；
- `history` 记录不参与 live 的 source、target 和 contract 唯一性校验；`duplicate` 必须有
  `canonical`/`superseded_by`，`legacy` 有替代者时填写 `superseded_by`，无替代者时填写
  `retirement_reason`；
- manifest 中不能出现不存在的路径；
- `platform-ctest` 必须声明平台门控；manifest 在所有平台注册由门控 wrapper 负责的 CTest
  条目，target 只在匹配平台生成，非匹配平台通过 `SKIP_RETURN_CODE=77` 稳定显示为 skipped；
- 通过 CTest labels 支持 domain、kind、risk 和 platform 定向执行。

执行资格矩阵如下：

| disposition | execution | 构建要求 | CTest 要求 |
| --- | --- | --- | --- |
| `active` | `ctest` | target 成功构建 | 必须注册并可定向运行 |
| `active` | `platform-ctest` | 匹配平台 target 成功构建；其他平台不生成 target | 所有平台均注册 wrapper；匹配平台运行，其他平台 skipped |
| `active` | `compile-only` | target 成功编译/链接 | 不注册 CTest |
| `diagnostic` | `manual` | 按需构建 | 不注册默认 CTest |
| `legacy` / `duplicate` | — | 不构建 | 仅保留 catalog 记录 |

构建源码列表仍然显式维护，不使用 glob 自动加入生产目标。文件系统扫描只用于治理校验。

现有 target 名称和链接闭包在“机械迁移 registry”阶段保持不变，避免外部脚本或现有回归
命令失效。后续 bundle 试点如果需要改变 target 名称，必须提供旧 target/CTest 名称到
新 bundle/case 的映射，并单独通过验收；“目标名改变”只在机械迁移阶段是回滚条件，不能
阻止已经批准的 bundle 试点。

## 6. 文档治理

### 6.1 生命周期

复用现有 docs 发布审计中的分类：

| 生命周期 | 含义 | 默认位置 |
| --- | --- | --- |
| `stable-current` | 描述当前代码的规范性事实 | `docs/specs/<domain>/` |
| `reusable-verification` | 可重复的测试和验收清单 | `docs/tests/` |
| `archive-legacy` | 已结束阶段和历史决策 | `docs/archive/` |
| `working` | research、TODO、plan、audit、handoff | 现有工作/审计目录 |

第一阶段只补元数据和索引，不批量移动文件。

每份当前或归档文档补充：

```text
lifecycle
owner
last_verified
canonical_id
code_anchors
supersedes
```

同一个 `canonical_id` 最多允许一份 `stable-current` 文档。元数据直接写在 Markdown
frontmatter 中；`docs/INDEX.md` 只是由这些元数据生成的索引，不是第二份事实源。`stable-current`
和 `archive-legacy` 要求完整填写 `owner`、`last_verified`、`canonical_id`、`code_anchors`
和 `supersedes`；`reusable-verification` 要求 `owner`、`last_verified`、`canonical_id`
和 `test_targets`；`working` 文档至少标记 `lifecycle`。

字段格式固定为：`lifecycle` 是上述枚举值；`owner` 和 `canonical_id` 是非空字符串；
`last_verified` 使用 `YYYY-MM-DD`；`code_anchors` 是仓库相对路径锚点列表，格式为
`path#L<line>`，历史文件可以使用 `commit:path#L<line>`；`test_targets` 是 target/CTest
名称列表；`supersedes` 是 ID 列表或 `null`。要求“填写”的字段即使没有值也必须显式写成
空列表或 `null`，不允许省略。`code_anchors`、`test_targets` 和 `supersedes` 的引用由
治理检查解析，不能只写自然语言描述。

### 6.2 初步文档处置方向

- `QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md`：基于当前 Runtime/Host 代码复核后保留为当前架构规范；
- `QML_UI_V2_PHASE1_TODO_ZH.md`：归类为工作状态清单，不再作为架构事实源；
- `QML_UI_V2_PHASE1_ARCHIVE_2026-09-05_ZH.md`：归类为历史归档；
- `V1_DEV_POST_V2_PORT_PLAN_2026-09-05_ZH.md`：迁移审计记录，不作为 v2 规范；
- `GPU_RENDERING_DEVICE_AND_QRHI_EXPORT_PLAN.md`：包含已删除 DComp/旧 MainWindow 方案，候选归档；
- `TIMELINE_LAYER_STACK_AND_SLIDE_ORDER_SPEC.md`：包含已删除 DComp 路径，候选归档或重写；
- `BOOKMARK_MAIDATA_SYNC_PLAN.md`：与当前书签规范合并，或作为历史计划归档；
- `QML_UI_V2_BACKEND_SURFACE_ZH.md`：保留其契约审计价值，但基于当前 Host 结构重写。

最终生成 `docs/INDEX.md`，让 [docs/README.md](/Users/caoyusen/Desktop/MiaCode/docs/README.md:1)
只承担入口、分类和维护规则说明。

## 7. 指引技能重建

### 7.1 唯一维护源

`.agents/skills/miacode-dev-guide/` 确定为唯一维护源，原因是该副本包含最近一轮
2026-09-05 的仓库结构更新。`.claude/` 和 `.codex/` 是自动生成的两个兼容镜像；不同
客户端若需要不同 frontmatter，只允许生成薄 adapter，不交换事实源角色。

不使用 Git symlink，以避免 Windows checkout 和不同客户端的兼容性问题。

### 7.2 新结构

`SKILL.md` 只保留路由、事实优先级、长期不变量和维护规则。详细内容放入 references：

- `architecture-and-layout.md`：当前 `src/app/qml_ui`、`src/app/runtime`、Host 和渲染架构；
- `feature-index.md`：用户功能到 owner、入口和规格的映射；
- `cross-chain-linkage.md`：parser、timeline、preview、audio、export、Muri 同步链；
- `design-ledger.md`：必须保持、允许调整和明确开放的产品契约；
- `build-and-tools.md`：CMake、CTest、规格 registry、打包和 helper；
- `debug-flags.md`：debug flag、日志和诊断入口；
- `hardcode-registry.md`：共享常量和阈值归属。

已删除路径只能出现在明确标记的 retired/migration 段落中。QML 视觉细节、阶段完成记录
和事故原始证据不再堆入主技能。

同步工具提供：

- `--sync`：从唯一源生成 `.claude` / `.codex` 镜像和索引；
- `--check`：比较文件集合和内容 hash，检查失效路径、失效 anchor 和重复 canonical ID；
- 兼容镜像只能有客户端所需的薄 frontmatter，正文不得单独编辑。

治理检查的默认范围是 `.agents/skills/miacode-dev-guide/`、其两个生成镜像、
`docs/specs/**/*.md`、`docs/tests/**/*.md`、`docs/archive/**/*.md`、`docs/audit/**/*.md`、
`docs/superpowers/plans/**/*.md`、`docs/superpowers/specs/**/*.md`、`docs/README.md`、
`CONTRIBUTING.md`、`CMakeLists.txt` 和 `cmake/devtools/`。这些 glob 扫描目录内的全部文件，
不以“已经有 lifecycle 字段”作为进入条件；受管 Markdown 缺少 frontmatter 或 lifecycle
时确定性失败。`docs/README.md` 和 `CONTRIBUTING.md` 使用各自的入口/贡献规则校验，不强制
套用规格 frontmatter。生成的 `.claude/.codex` 镜像、`docs/INDEX.md` 和
`docs/tests/SPEC_CATALOG.md` 通过 `docs/.governance-exemptions.yml` 逐路径登记为生成物，
只检查其生成结果与源输入一致；任何新增豁免必须同时填写原因、生成命令和 owner。历史
目录仍检查生命周期、链接和历史锚点，但不把历史路径当作当前事实。

### 7.3 规格审计账本与 live registry

registry 文件分为 live entries 和 history ledger 两个逻辑区段。构建只消费 live 区段中
`active` 和 `diagnostic` 条目，其中 `active + execution=compile-only` 只要求构建验证；
history ledger 的 `entry_type=history` 永不生成 target，也不要求 source 文件存在。live
区段的唯一性只对当前 canonical owner 和 bundle case 生效；history 行即使保留原来的
`contract_id`，也不参与该校验。`duplicate` history 行必须填写 `canonical` 和
`superseded_by`；`legacy` 有替代者时填写 `superseded_by`，没有替代者时填写
`retirement_reason`，不能要求一个不存在的替代规格。

生成的 `docs/tests/SPEC_CATALOG.md` 展示全部 111 个原始项目，包含已删除、已合并和已
改为 diagnostic 的记录。这样“live registry 的路径必须存在”和“原始 111 项全部有结论”
是两个互不冲突的验收条件。

## 8. 实施阶段

### 阶段 0：基线

- 建立 111 项规格清单：source、target、CTest、spec_id、contract、平台、风险和候选状态；
- 保存迁移前 target 名称、CTest 名称、链接闭包和 Release 结果；
- 确认外部脚本是否依赖现有 target 名称。

### 阶段 1：先重建指引

- 基于当前代码和 `src/app/runtime/ASSEMBLY.md` 重建唯一维护源；
- 清理旧路径和 v1 产品事实；
- 生成并验证两个客户端兼容镜像；
- 更新 `CONTRIBUTING.md`，明确唯一维护源和生成规则。

### 阶段 2：文档索引

- 为当前 docs 添加生命周期和 canonical ID；
- 生成文档索引；
- 先不删除和移动大批文件；
- 对明确过时的 DComp/旧 MainWindow 文档做单独处置。

### 阶段 3：规格 registry

- 将现有 CMake 声明机械迁移到 domain manifests；
- 保持 target 名称、链接闭包和 CTest 行为不变；
- 增加集合校验和 CTest labels；
- `platform-ctest` 必须声明平台门控；非匹配平台显示为 skipped，不得被误报为失败；
- 确认 `MIACODE_BUILD_DEV_TOOLS=OFF` 时产品构建不变。

### 阶段 4：分类和清理

- 按逐断言映射处理 legacy、duplicate 和 diagnostic；
- 每个删除或改名保持独立提交；
- diagnostic 改为 probe/tool，不再进入默认 CTest；
- compile-only 规格必须明确标注其构建目的；
- `risk=high` 默认保持独立，任何 bundle 或 active→diagnostic/duplicate/legacy 的转换都
  必须附带可验证的 `risk_waiver`，否则治理检查失败。

### 阶段 5：小范围合并试点

- 选择一组同 owner、同依赖、纯函数性质的微型规格；
- 增加 case 定向执行和独立失败输出；
- 比较构建耗时、执行耗时、失败定位和维护复杂度；
- 只有在至少减少 2 个 executable、clean Release dev-tool 的 configure 和 build 时间中位数
  各自都改善至少 10%、且 targeted case、CTest 标签和失败定位均无回归时，才继续扩大合并；否则停止
  继续合并并保留 registry/index 治理成果。

耗时比较使用同一主机、工具链、Release 配置和 `MIACODE_BUILD_DEV_TOOLS=ON`；基线和
试点分别从空的构建目录开始，各测 3 次，分别记录 configure 和 build wall-clock time，
以中位数比较。执行期间不改变并行度、编译器、Qt 或缓存策略。若只能测到其中一项，不能
宣称达到 10% 的完整门槛。

## 9. 验收与回滚

验收标准：

- 原始 111 项全部在 audit catalog 中有明确 disposition，无遗漏、无双重注册；
- live registry 中的所有非历史记录 source 均存在且恰好注册一次；
- active contract 都有可定向验证入口；`execution=compile-only` 的规格以目标成功编译/链接
  作为验证，不要求 CTest 运行；
- 所有 `risk=high` 记录默认保持独立；若存在 bundle 或降级，必须有完整且可解析的
  `risk_waiver`，包括审批人、日期、逐断言证据和回滚引用；preview/audio/export/v2 的高风险
  边界还必须在逐断言审查中单独确认；
- registry 机械迁移前后 target、CTest 和链接闭包等价；bundle 试点另有旧名映射和单独
  验收记录；
- 一个维护源加两个生成技能镜像通过 hash/路径检查；
- 当前文档不存在未标注的 v1 产品事实；
- 同一 `canonical_id` 不存在多个当前规范；
- 新增规格、当前文档或技能变更若未登记，治理检查可确定性失败。

以下情况触发回滚：

- 在机械 registry 迁移阶段改变现有 target/CTest 名称或过滤行为；bundle 试点未提供
  旧名映射或兼容策略；
- 合并后失去编译/链接隔离；
- 一个 case 崩溃导致其他契约无法运行；
- preview/audio/export 同步规格出现非预期变化；
- 任一客户端无法加载生成镜像；
- 文档移动造成大量不可恢复的链接破坏。

回滚单位保持独立：registry 可退回原 CMake 声明，bundle 可拆回独立 executable，文档
先加元数据后移动，镜像生成失败时恢复薄 adapter 而不是恢复手工副本。

## 10. 备选方案与取舍

### 规格测试

1. **只增加手写索引**：改动小，但 CMake、索引和生命周期仍会漂移；不推荐。
2. **所有规格合并为一个 Runner**：目标数量最少，但会膨胀链接依赖、污染全局状态，并
   失去边界规格的编译/链接保护；拒绝。
3. **Registry + 默认独立目标 + 少量 bundle 试点**：保留隔离，同时解决发现和重复声明；
   推荐。

### 技能维护

1. **直接删除两套技能**：结构简单，但可能破坏客户端发现；风险偏高。
2. **`.agents` 单一源，其他目录自动生成**：兼容性和治理成本平衡最好；推荐。
3. **新增中立 knowledge base，再由三套技能引用**：长期更干净，但初次迁移较大，且依赖
   各客户端是否支持外部引用；作为后续候选。

## 11. 初步批准记录

用户已同意以下方向：

- 不追求把 111 个规格强行压成固定数量；
- 保留必要的独立编译/链接/进程边界；
- 使用 registry 和分类治理发现成本；
- 只对同 owner、同依赖的小型策略规格做合并试点；
- 暂定 `.agents` 为唯一技能维护源，`.claude/.codex` 为生成镜像。

本设计已由用户在 2026-09-06 授权实施，并允许按判断简化；实际范围以上方「实施取舍」及实施结果为准。
