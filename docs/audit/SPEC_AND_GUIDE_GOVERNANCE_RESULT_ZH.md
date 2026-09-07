---
lifecycle: working
---

# Spec 与开发指引整理结果

日期：2026-09-06。基线：`9f5f4607`。本记录说明本次迁移与验证，不是需要随代码日常更新的规范。
用户授权按判断调整设计，重点是降低 skill 的复杂度和维护频率。

## 落地内容

- 111 个独立 Spec 在 `cmake/devtools/specs/` 注册，110 个保持 CTest。新增 owner、稳定 contract ID、domain/kind/risk/platform 与执行方式，源码和依赖只在 CMake 维护一次。
- CMake 校验漏登记、重复 source/target/contract、失效路径及执行方式；生成 `docs/tests/SPEC_CATALOG.md`。未删除、重命名、合并任何 Spec，也未修改生产行为或可执行断言。
- `playback_coordinator_construction_spec` 保留既有 `EXCLUDE_FROM_ALL` 和无 CTest 状态。原源文件闭包未完成链接，目录标为 `blocked-link`，不能当作已通过的 compile-only 测试。
- `.agents/skills/miacode-dev-guide/` 是唯一维护源；32 行入口与三份短参考合计 130 行。保留仓库归属、可复用 QML 组件/服务、跨模块同步、验证入口；移除易漂移的逐函数与参数清单。
- `.claude` 与 `.codex` 同名 skill 为字节一致的生成镜像，包含原有 Codex UI 元数据。
- 公开文档标明生命周期；现行应用与渲染契约单独提炼，backend 清册改为 Session 边界。历史内容保留原路径，混合待复核内容标为 working；不批量宣称旧方案已经实现。
- 文档索引忽略本地私有记录；当前文档校验稳定 ID、owner、代码锚点、文件链接，验收清单校验 target。工具使用 Python 标准库与 Git/CMake，无需新增项目 Python 依赖。

## 验证结果

| 检查 | 结果 |
| --- | --- |
| 同步镜像、文档索引、Spec catalog 的只读检查 | 通过 |
| skill-creator 格式校验 | 通过；校验器的 PyYAML 仅装入临时目录，未添加项目依赖 |
| 治理回归测试 | 20/20 通过，涵盖漏登记、重复、失效引用、生成漂移、中文忽略路径 |
| 独立 skill 检索复核 | 设置控件复用、note 同步链、新 Spec 定向验证三个场景通过 |
| macOS/Windows/Linux 条件分支的离线 CMake 声明比较 | 153/156/149 条 target/source/link/include/definition/property/command 记录分别一致 |
| 实际 macOS Release 配置迁移比较 | 115 个开发目标的链接、编译选项、依赖信息和 source→object 集合一致；110 个 CTest 名称、命令与既有属性一致，仅新增标签 |
| DEV_TOOLS=ON 配置 | 通过 |
| 独立目录 DEV_TOOLS=OFF 配置 | 通过，CTest 数量为 0 |
| 相关 11 个 Release target 构建 | 通过 |
| 完整现有 CTest 集合 | 106/110 通过；下列 4 个失败重新构建后仍复现 |

domain 分组改变了 CTest 的枚举顺序；使用名称或标签过滤，不依赖数字编号。
跨平台条件分支比较不等于 Windows/Linux 实机编译或硬件验收。
本次未做全量 clean build、性能基准、bundle 试点或图形/设备人工验收。

## 已有失败与后续边界

以下检查涉及的生产源码与 Spec 断言均未被本次修改；不能为了目录整理而弱化断言，或把失败归为 diagnostic 跳过。

| 测试 | 当前失败 |
| --- | --- |
| `ui_text_locale_spec` | 内置语言表缺少 `metadata.manage_per_difficulty_designers`，部分 QML 文案未解析到目录键 |
| `qml_editor_controller_spec` | Ctrl/Command 点击后的 preview seek 行为断言失败 |
| `timeline_model_spec` | 源码守卫仍匹配旧 `+ 2` 边距字面式；已有 `3fd485d7` 改为共享 Theme 间距 |
| `qtavplayer_platform_spec` | prepared playback commit 的 seek/start 源码守卫失败 |

其中 backend 清册依赖文档格式，已保留旧计数标签的兼容解释，并确认 `qml_ui_backend_surface_spec` 通过。
四项失败是现有回归清单，产品或测试修正应按各自契约处理，不纳入本次机械迁移成功的证明。

## 复用验证入口

```sh
python3 scripts/governance/sync_guides.py --check
python3 scripts/governance/docs_index.py --check
cmake -DMIACODE_SPEC_CATALOG_CHECK=ON -P cmake/devtools/SpecCatalog.cmake
python3 -m unittest discover -s scripts/governance -p '*_test.py'
ctest --test-dir <dev-build> -C Release --output-on-failure
```

后续只有实际出现需要退役/合并的规格，才补该契约的覆盖证据与迁移记录；不提前维护完整审批、history 或 runner 框架。
