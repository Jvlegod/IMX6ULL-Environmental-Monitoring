# 设备监控与远程控制扩展实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为服务端, 网页和 Qt 客户端增加遥测历史, 趋势图, CSV 导出, 设备使能控制和可靠 OTA 操作反馈.

**架构：** 服务端在接收遥测时写入 PostgreSQL, 通过历史查询和 CSV 接口提供数据. 网页使用现有设备 API 和新增历史 API 绘制趋势并导出. Qt 沿用串行任务队列接收 `set_device_enabled` 命令, 控制采集上传开关.

**技术栈：** Rust, Axum, SQLx/PostgreSQL, HTML/CSS/JavaScript, Qt Widgets/C++11.

---

### 任务 1：服务端遥测历史与导出 API

**文件：**
- 修改：`server/src/main.rs` 的 AppState, telemetry handler, router.
- 修改：`server/Cargo.toml` 增加 CSV 序列化依赖（若现有依赖无法直接生成 CSV）.
- 创建：`server/migrations/` 下的遥测表迁移（若项目已有迁移目录, 使用现有目录）.
- 测试：`server/tests/telemetry_history.rs`.

- [ ] **步骤 1：编写失败测试**
  覆盖遥测 POST 后历史 GET 返回同一条记录, 时间范围过滤, CSV 响应包含表头和设备 ID.
- [ ] **步骤 2：运行测试确认失败**
  运行 `cargo test --manifest-path server/Cargo.toml telemetry_history`.
- [ ] **步骤 3：实现最少代码**
  增加 `telemetry_records` 表, 在数据库可用时插入记录, 实现 `GET /api/v1/devices/{device_id}/telemetry` 和 `.csv`, 校验 `from`, `to`, `limit`.
- [ ] **步骤 4：运行测试确认通过**
  运行 `cargo test --manifest-path server/Cargo.toml`.
- [ ] **步骤 5：Commit**
  `git add server && git commit -s -m "新增: 保存遥测历史并支持 CSV 导出"`.

### 任务 2：网页趋势图, 导出和 OTA 按钮状态

**文件：**
- 修改：`server/static/index.html`（或当前网页入口文件）增加趋势面板, 时间范围, 导出按钮和设备使能开关.
- 修改：`server/static/app.js` 增加历史请求, 图表渲染, CSV 下载, 上传状态机.
- 修改：`server/static/style.css`（若存在）优化卡片布局和禁用态.
- 测试：浏览器手工测试和 `curl` API 检查.

- [ ] **步骤 1：实现 OTA 按钮状态机**
  文件未选择, 上传中, 创建任务中均设置 `disabled=true`, 失败恢复可用并显示错误.
- [ ] **步骤 2：实现趋势查询和绘制**
  设备选择或时间范围变化时请求历史 API, 使用已有前端图表依赖或轻量 SVG 绘制四条折线.
- [ ] **步骤 3：实现 CSV 下载**
  根据当前设备和时间范围生成 `.csv` API URL, 使用浏览器下载.
- [ ] **步骤 4：实现设备使能开关**
  开关调用现有命令接口发送 `set_device_enabled` 并显示创建的命令 ID.
- [ ] **步骤 5：验证并 Commit**
  使用 `curl --noproxy '*'` 验证历史和 CSV 响应, 浏览器验证按钮禁用, 趋势更新和下载, 然后提交 `git commit -s -m "新增: 网页趋势导出和设备控制"`.

### 任务 3：Qt 设备使能命令与控件布局

**文件：**
- 修改：`esp8266controller.cpp` 和 `.h` 处理 `set_device_enabled`.
- 修改：`sensorprovider.cpp` 和 `.h` 增加设备使能状态并跳过禁用设备采集上传.
- 修改：`mainwindow.cpp` 和 `.h` 将阈值, 采样时间, 采样周期控件整理为统一分组.
- 测试：`cmake --build build-arm -j2` 和开发板串口日志.

- [ ] **步骤 1：添加命令处理测试场景**
  准备服务端命令 JSON, 验证 Qt 日志输出 `set_device_enabled` 和对应状态信号.
- [ ] **步骤 2：实现控制器信号**
  增加 `remoteDeviceEnabled(QString,bool)` 信号, 在命令分支校验设备 ID 和 enabled.
- [ ] **步骤 3：实现采集开关**
  SensorProvider 保存每个设备 enabled 状态, 禁用时不生成快照或上传, 重新启用后恢复.
- [ ] **步骤 4：调整 Qt 布局**
  用分组框和网格布局统一阈值, 采样时间, 周期和设备开关, 保持现有信号连接.
- [ ] **步骤 5：构建验证并 Commit**
  运行 `cmake --build build-arm -j2`, 检查无编译错误, 提交 `git commit -s -m "新增: Qt 设备使能控制和参数布局"`.

### 任务 4：端到端验证与文档

**文件：**
- 修改：`README.md` 增加历史 API, CSV 导出和设备使能测试步骤.
- 修改：`OTA_GUIDE.md` 补充 OTA 按钮状态和失败恢复说明.

- [ ] **步骤 1：启动服务端并发送三条不同时间遥测**
- [ ] **步骤 2：验证网页趋势, CSV 内容和设备使能命令日志**
- [ ] **步骤 3：验证 Qt 禁用设备后不再上传, 重新启用后恢复**
- [ ] **步骤 4：运行 Qt 构建和 Rust 测试**
- [ ] **步骤 5：Commit**
  `git commit -s -m "文档: 补充监控扩展测试流程"`.
