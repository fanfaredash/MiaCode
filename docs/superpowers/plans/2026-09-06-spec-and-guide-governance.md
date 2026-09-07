---
lifecycle: working
---

# Spec 与开发指引整理实施计划

基于 `9f5f4607` 的设计及用户授权，以降低维护成本为取舍依据。

## 实施范围

- [x] 将现有 Spec 构建声明移入 domain manifests，保留 source、target、依赖和 CTest 行为；增加 owner、contract、kind 与执行方式的轻量注册校验和标签，生成规格目录。
- [x] 重写 `.agents/skills/miacode-dev-guide/`，只保留模块边界、复用入口、同步关系和验证入口；自动同步 `.claude` / `.codex`，清除旧参考副本。
- [x] 文档标明 current、verification、archive 或 working；生成索引。当前架构与 backend surface 以代码复核，旧方案明确标为历史。
- [x] 更新 CONTRIBUTING、docs 入口和原设计的实施取舍；执行同步/索引检查、负例验证、CMake/CTest 注册等价检查和相关 Release 验证。

## 简化决策

保持全部现有 Spec 的断言和独立进程，不按名称判重或合并。没有实际退役或 bundle 时不实现 history/waiver/bundle 框架。链接依赖只在 CMake 声明一次；路径锚点使用稳定文件/目录，不维护行号。工作记录只需生命周期；只对已复核当前规范建立 canonical ID，不把历史提案批量认定为当前事实。生成文件不另建豁免登记系统。

## 验证

新增治理工具须使用 Python 标准库或 CMake 本身，能够离线执行；验证漂移、漏登记、重复入口和失效引用的失败行为。规格迁移前后比较实际 target/source/link 与 CTest 配置。只运行与改动有关的验证，不清理现有构建产物，不生成无证据的全平台通过声明。

## 完成记录

实施、取舍、通过项与未改动的四项既有测试失败见 [整理结果](../../audit/SPEC_AND_GUIDE_GOVERNANCE_RESULT_ZH.md)。
