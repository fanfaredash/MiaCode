---
lifecycle: stable-current
owner: src/app/ui
canonical_id: ui.backend-surface
last_verified: 2026-09-06
code_anchors: ["src/app/ui/Bootstrap.cpp", "src/app/runtime/Session.h", "src/tools/ui/QmlUiBackendSurfaceSpec.cpp"]
---

# QML 到 Session 的直接访问边界

产品前端通过 ApplicationServices 的服务/typed slots 访问领域能力。
仅 Bootstrap 在创建、附着和激活根窗口时直接调用 Session。
Session 是 QObject 装配对象；旧 MainWindow 产品窗口已经删除。

`qml_ui_backend_surface_spec` 扫描 QML C++ 源码与 Session 头文件，对本文清单做集合相等比较。
下面计数字段中的 `MainWindow` 是该守卫仍使用的历史格式标签，实际被检查的类型是 Session；
保留标签避免在文档整理中同时改变可执行断言。

> 方法 **4**，直接读取的 `MainWindow` 私有成员 **0**，friend 授权 **0** 个 QML 类型。

## friend 授权

QML 类型没有 Session 的 friend 授权；领域行为应经已有端口调用。

## 清单

`src/app/ui/Bootstrap.cpp` 的根窗口生命周期调用：

- `attachRootWindow`
- `noteRootWindowReady`
- `setBackendActive`
- `setRootWindowFrameGeometry`

列表按去重方法名计数，不按调用次数计数。文档、播放、预览、导出等业务调用不属于此清单，
它们通过应用服务与端口进行，见 [当前架构](CURRENT_ARCHITECTURE_ZH.md)。

## 更新规则

直接调用集合改变时，同步调整清单并运行 `qml_ui_backend_surface_spec`。
新增业务需求先检查现有 engine/port，不把业务入口加回 Session。
旧逐阶段削减记录可从本文件 Git 历史查阅，不作为当前所有权依据。
