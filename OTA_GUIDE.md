# WiFi OTA 升级说明

本文说明 i.MX6ULL 环境监测系统当前的应用 OTA（Over-The-Air）和系统 OTA 流程. 文档以仓库中的实际代码为准, 明确区分已可用, 模拟和未完成部分.

## 1. OTA 类型和边界

### 1.1 应用 OTA

应用 OTA 替换 `/usr/bin/environment_monitor`. 下载先保存到 `/tmp/environment_monitor.new`, 校验通过后由 `scripts/environment_monitor_ota_apply.sh` 停止旧进程, 保留备份, 替换文件并重启应用. 脚本优先写 `/var/log/`, 不可写时回退到 `/tmp/`.

### 1.2 系统 OTA

网页可分别上传 `uboot`, `kernel`, `dts` 和 `rootfs`. 服务端生成系统 manifest, ESP8266 将文件保存到 `/mnt/boot/update`, 完成大小和 SHA256 校验并生成 `manifest.sha256`. 当前未实现实际 MTD 分区写入, 启动模式切换和 recovery 回滚.

## 2. 电脑端准备

电脑和开发板必须在同一网段. 应用 OTA:

```bash
cd /home/jvle/Desktop/works/IMX6ULL/porting/environment_monitor_qt
./scripts/start_ota_server.sh -v 0.2.3 -p 18080
cat ota/manifest.json
sha256sum ota/environment_monitor
curl http://电脑IP:18080/manifest.json
```

脚本会加载 ARM Qt 工具链, 构建 `environment_monitor`, 写入 `ota/environment_monitor`, 运行 `scripts/make_ota_manifest.py` 生成 `ota/manifest.json`, 再启动 Python HTTP server.

Rust 网关网页 OTA 需要先构建前端:

```bash
cd server/web
npm install
npm run build
cd ..
cargo run
```

网页调用 `POST /api/v1/ota/upload` 保存文件到 `data/ota/<uuid>`, 再向设备命令接口提交 `kind: "ota"`. 系统 OTA 先上传各 artifact, 再调用 `POST /api/v1/system-updates` 创建 manifest, 提交 `kind: "system_update"`.

## 3. 应用 OTA 调用链

```text
电脑 HTTP server
  -> manifest.json / environment_monitor
Esp8266Controller::startOta
  -> AT+CIPMODE=0, AT+CIPCLOSE, AT+CIPSTART, AT+CIPSEND
ESP8266 +IPD HTTP 响应
  -> manifest 解析
/tmp/environment_monitor.new
  -> 文件大小 + SHA256
otaPackageReady
  -> WifiDialog::applyOtaPackage
scripts/environment_monitor_ota_apply.sh
  -> 备份, 替换, 重启
新 Qt 应用
```

`startOta()` 检查串口, 服务器地址, manifest 路径和当前操作, 清空旧状态后进入 `beginOtaConnection()`. `readAvailable()` 识别 `+IPD,<length>:`，等待完整 payload 后交给 `processIpdPayload()`，支持 HTTP 响应被拆成多个串口片段. `processHttpData()` 解析 HTTP 头和 `Content-Length`, 将 body 写入 manifest 缓冲区或临时文件, 并持续发出 `otaProgress()`.

`finishHttpResponse()` 比较文件大小, 使用 `QCryptographicHash::Sha256` 校验摘要, 失败时删除临时文件, 成功时发出 `otaPackageReady(version, "/tmp/environment_monitor.new")`.

## 4. 系统 OTA 链路

网页为每个系统文件调用上传接口, 服务端生成包含 `device_id`, `version` 和 `artifacts` 的 manifest. ESP8266 创建 `/mnt/boot/update`, 逐个下载, 校验并将摘要追加到 `manifest.sha256`. 设备端目前只负责下载和校验, 生产部署前必须补充分区写入, 掉电保护, 启动确认和失败回滚.

## 5. 排障

| 现象 | 检查 | 处理 |
| --- | --- | --- |
| 网关网页 404 | `server/web/dist` | 运行 `npm run build` |
| `/healthz` 正常但设备失败 | 开发板到电脑 IP, 端口 | 确认可访问 `电脑IP:18080` |
| manifest 404 | HTTP 根目录和请求路径 | 确认路径为 `/manifest.json` |
| `+IPD` 解析失败 | 波特率, TX/RX, 共地 | 检查 115200 和串口接线 |
| SHA256 失败 | manifest 和下载文件 | 重新执行 `sha256sum` |
| 新程序未启动 | apply 日志, 权限, 备份 | 检查 `/tmp/environment_monitor_ota_apply.log` |
## 7. OTA 前的内核和设备确认

OTA 只替换 Qt 应用, 不会替换内核里的传感器驱动。开发板上的 BMP580 和 VEML7700 是否能工作, 取决于当前启动的设备树和内核配置。对应源码在相邻目录 `../linux-imx-rel_imx_4.1.15_2.1.0_ga/`:

- BMP580: `drivers/iio/pressure/bmp580-i2c.c` 和 `bmp580-spi.c`
- VEML7700: `drivers/iio/light/veml7700.c`
- 设备树: `arch/arm/boot/dts/imx6ull-14x14-evk-emmc.dts`
- 通用 UART: `drivers/tty/serial` 和 `arch/arm/boot/dts/imx6ull.dtsi`

升级后可以先确认内核设备仍然存在:

```bash
dmesg | grep -Ei 'bmp580|veml7700|i2c|uart|ttymxc'
find /sys/bus/iio/devices -maxdepth 2 -name name -exec sh -c 'printf "%s: " "$1"; cat "$1"' _ {} \\\;
ls -l /dev/ttymxc*
```

如果 IIO 目录没有出现, 问题通常在设备树 `compatible`、I2C 地址、Kconfig 或内核启动参数, 和 Qt OTA 替换脚本不是同一层的问题。
