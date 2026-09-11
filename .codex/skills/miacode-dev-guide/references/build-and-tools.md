# 开发与验证

## 构建与 Spec

- 依赖和平台入口查 `README.md`、`scripts/README.md`；Windows/macOS 脚本位于 `scripts/build/`。Linux 预览解码用主机 pkg-config 的 FFmpeg 与 libva，音频用 `third_party/bass/lib/linux/`；导出独立 `ffmpeg` 由 `scripts/ffmpeg/ensure-linux-ffmpeg.sh` 准备。
- Linux 产品可执行文件生成到 `build/bin/MiaCode`；Qt 按模块 URI 管理构建期 QML 目录。
- 日常构建使用 Release；复用已配置的构建目录，先检查 CMakeCache，不硬编码开发者机器路径。
- 可执行规格由 `MIACODE_BUILD_DEV_TOOLS=ON` 启用；domain manifests 位于 `cmake/devtools/specs/`，注册规则查 `cmake/devtools/MiaCodeSpecRegistry.cmake`。
- 新 Spec 仿照同域的 `miacode_add_spec`，声明保护的契约与 owner，复用源文件分组，保留最小链接依赖。手动诊断仍用 `miacode_add_dev_tool`。
- 查 `docs/tests/SPEC_CATALOG.md` 选择 target；compile-only 规格以构建验证，不要求 CTest 条目。

定向验证（将占位符换成实际目录与 target）：

```sh
cmake --build <dev-build> --config Release --target <spec-target> --parallel 4
ctest --test-dir <dev-build> -C Release -R '^<spec-target>$' --output-on-failure
```

遵循 `miacode-concurrent-build` skill 的并发上限与产物保护规则；同仓库不要并行启动两次构建。
只改文档时运行治理检查即可；修改 CMake 注册时检查配置与测试集合，改行为时运行相关规格。

## 调试、共享值与资源

- 运行时诊断使用 `--debug`；日志入口是 `src/common/DebugLog.h`，flag 的维护索引是 `docs/ops/DEBUG_INDEX.md`。skill 不复制 flag 清单。
- 行为参数先在所属模块及 `src/common/` 的 Config/Settings 头中查找。跨模块共享语义复用同一 helper；局部实现值留在局部。
- 资源根、谱面素材解析复用 AssetPaths、ChartAssetPaths、ChartMediaService；资源声明在 `resources/`，打包入口在 `scripts/build/`。
- 翻译源文件是 `translations/{en_US,zh_CN,ja_JP}.ts`，生成的 QM 嵌入 `/i18n` 资源前缀。
- 新资源/命名规则检查 preview、export、工具和打包消费者；不提交本地产物、日志、第三方二进制。

## 文档与 skill

- 当前公开契约与工作/历史文档在 `docs/INDEX.md` 区分；维护规则查 `docs/README.md`。
- `python3 scripts/governance/sync_guides.py --sync` 更新两个客户端镜像；`--check` 检查漂移与 skill 引用。
- `python3 scripts/governance/docs_index.py --sync` 生成文档索引；`--check` 验证元数据、当前锚点和索引。
- skill 只维护稳定的地图、复用点和同步关系。完整 flag、target、参数表从各自代码/索引查找，不新增并行手写清单。
