# WiFi OTA 升级说明

本文档说明 i.MX6ULL 环境监测系统当前的 WiFi OTA 应用升级流程.

## 1. OTA 能升级什么

当前 OTA 只升级 Qt 应用程序:

```text
/usr/bin/environment_monitor
```

OTA 不会升级以下内容:

- Linux 内核
- 设备树文件 `*.dtb`
- Qt 运行库
- 其他 rootfs 文件

首次部署或 Qt 运行环境发生变化时, 仍然需要使用 MFGTool 烧录完整 rootfs.

## 2. 首次部署

OTA 修复版程序必须先通过 MFGTool 烧录到开发板. 当前程序使用板上的 UART4:

```text
开发板串口: /dev/ttymxc3
波特率: 115200
格式: 8N1
```

mini-rootfs 重新生成的文件位于:

```text
/home/jvle/Desktop/works/IMX6ULL/porting/mini-rootfs/out/rootfs.tar.bz2
```

该文件已经同步到 MFGTool 的 rootfs 目录. 烧录后, rootfs 中应存在:

```text
/usr/bin/environment_monitor
/usr/bin/environment_monitor_ota_apply.sh
```

## 3. 启动 OTA 服务器

电脑和开发板必须连接到同一个局域网. 在电脑上执行:

```bash
cd /home/jvle/Desktop/works/IMX6ULL/porting/environment_monitor_qt
./scripts/start_ota_server.sh -v 0.2.3 -p 18080
```

脚本会自动完成以下操作:

1. 加载 Qt ARM 交叉编译环境
2. 编译 ARM 版本 Qt 程序
3. 将程序复制到 `ota/environment_monitor`
4. 生成 `ota/manifest.json`
5. 启动 HTTP 文件服务器

版本号可以按需要修改, 例如:

```bash
./scripts/start_ota_server.sh -v 0.2.4 -p 18080
```

服务器启动后会打印电脑的局域网 IP. 这个 IP 才是 Qt 界面中的 OTA 服务器地址.

不要填写开发板 IP `192.168.43.7`, 该地址属于开发板.

## 4. Qt 界面配置

进入 Qt 的 `WiFi 配置`, 填写:

```text
OTA 服务器: 电脑的局域网 IP
HTTP 端口: 18080
Manifest 路径: /manifest.json
```

先确认 ESP8266 已经连接 WiFi, 状态中出现 IP 地址, 再点击 `检查并升级应用`.

升级过程中不要同时执行以下命令:

```sh
cat /dev/ttymxc3
```

也不要在 Qt 程序运行时手动向 `/dev/ttymxc3` 发送 AT 指令, 否则会和 Qt 程序抢占同一个串口.

## 5. OTA 实际流程

Qt 程序通过 ESP8266 的普通 TCP 模式完成 HTTP 通信:

1. 发送 `AT+CIPMODE=0`, 强制退出透明传输模式
2. 发送 `AT+CIPCLOSE`, 清理上一次可能残留的 TCP 连接
3. 发送 `AT+CIPSTART` 连接电脑的 HTTP 服务器
4. 发送 `AT+CIPSEND` 请求发送 HTTP 数据
5. 收到 `>` 后发送 `GET /manifest.json`
6. 读取 manifest 中的版本号, 文件路径, 文件大小和 SHA256
7. 再次建立 TCP 连接并下载 `environment_monitor`
8. 将文件保存为 `/tmp/environment_monitor.new`
9. 校验文件大小和 SHA256
10. 执行 `/usr/bin/environment_monitor_ota_apply.sh`
11. 备份旧程序, 替换新程序并重启 Qt 应用

服务器正常时, 电脑终端应出现类似日志:

```text
GET /manifest.json
GET /environment_monitor
```

## 6. 升级失败排查

### 服务器没有任何日志

检查以下内容:

- Qt 中是否填写了电脑 IP, 不是开发板 IP
- HTTP 端口是否为 `18080`
- manifest 路径是否为 `/manifest.json`
- 开发板是否已经显示 WiFi IP
- 当前运行的是否是最新烧录的程序

### 服务器收到 `AT+CWJAP...`

说明开发板运行的还是旧程序, 或 ESP8266 仍处于透明传输模式. 重新烧录最新 rootfs 后再测试.

### Qt 仍然提示响应超时

停止 Qt 程序后, 可以通过串口确认 ESP8266 已退出透明模式:

```sh
stty -F /dev/ttymxc3 115200 cs8 -cstopb -parenb -ixon -ixoff -crtscts

rm -f /tmp/esp8266-mode.log
cat /dev/ttymxc3 > /tmp/esp8266-mode.log &
reader_pid=$!
sleep 1
printf 'AT+CIPMODE?\r\n' > /dev/ttymxc3
sleep 2
kill "$reader_pid" 2>/dev/null
cat /tmp/esp8266-mode.log
```

正常结果应包含:

```text
+CIPMODE:0
OK
```

然后确认 WiFi:

```sh
rm -f /tmp/esp8266-wifi.log
cat /dev/ttymxc3 > /tmp/esp8266-wifi.log &
reader_pid=$!
sleep 1
printf 'AT+CIFSR\r\n' > /dev/ttymxc3
sleep 2
kill "$reader_pid" 2>/dev/null
cat /tmp/esp8266-wifi.log
```

应能看到 `+CIFSR:STAIP` 和开发板 IP.

## 7. 手工准备 OTA 文件

如果不使用一键脚本, 可以手工生成 OTA 目录:

```bash
cd /home/jvle/Desktop/works/IMX6ULL/porting/environment_monitor_qt
mkdir -p ota
cp build-arm/environment_monitor ota/environment_monitor
python3 scripts/make_ota_manifest.py \
    --file ota/environment_monitor \
    --version 0.2.3 \
    --path /environment_monitor \
    --output ota/manifest.json
cd ota
python3 -m http.server 18080 --bind 0.0.0.0
```

## 8. 当前文件位置

- OTA 控制器: `esp8266controller.cpp`, `esp8266controller.h`
- 一键 OTA 脚本: `scripts/start_ota_server.sh`
- OTA 替换脚本: `scripts/environment_monitor_ota_apply.sh`
- manifest 生成脚本: `scripts/make_ota_manifest.py`
- OTA 文件目录: `ota/`

## 9. 参考链接

- [Espressif ESP8266 TCP/IP AT 指令](https://docs.espressif.com/projects/esp-at/en/latest/esp8266/AT_Command_Set/TCP-IP_AT_Commands.html)
- [Espressif ESP8266 基础 AT 指令](https://docs.espressif.com/projects/esp-at/en/latest/esp8266/AT_Command_Set/Basic_AT_Commands.html)
- [Python HTTP Server](https://docs.python.org/3/library/http.server.html)

## 10. 本项目的 OTA 实现

本项目没有使用 Qt 自带的网络接口直接连接 WiFi. Qt 程序通过 UART4 控制 ESP8266, 再由 ESP8266 的 TCP 功能访问电脑上的 HTTP 服务器.

### 10.1 Qt 端控制器

文件:

```text
/home/jvle/Desktop/works/IMX6ULL/porting/environment_monitor_qt/esp8266controller.cpp
/home/jvle/Desktop/works/IMX6ULL/porting/environment_monitor_qt/esp8266controller.h
```

`Esp8266Controller` 负责串口配置, AT 指令发送, ESP8266 响应解析和 OTA 下载. OTA 使用状态机控制流程:

```text
Idle
  -> OtaSettingMode
  -> OtaClosing
  -> OtaConnecting
  -> OtaWaitingPrompt
  -> OtaReceiving
  -> Idle
```

状态机的作用是确保每一条 AT 指令都在上一条指令完成后发送. 其中:

- `OtaSettingMode` 发送 `AT+CIPMODE=0`, 退出透明传输模式
- `OtaClosing` 发送 `AT+CIPCLOSE`, 清理旧 TCP 连接
- `OtaConnecting` 发送 `AT+CIPSTART`, 连接电脑服务器
- `OtaWaitingPrompt` 发送 `AT+CIPSEND`, 等待 ESP8266 返回 `>`
- `OtaReceiving` 解析 ESP8266 的 `+IPD` 数据包, 提取 HTTP 响应

### 10.2 HTTP 请求方式

ESP8266 工作在普通 TCP 模式, Qt 程序自己组装最小 HTTP/1.1 请求:

```text
GET /manifest.json HTTP/1.1
Host: 电脑IP
Connection: close
```

程序先下载 manifest, 再从 JSON 中读取:

```json
{
  "version": "0.2.3",
  "path": "/environment_monitor",
  "size": 123456,
  "sha256": "..."
}
```

随后程序根据 `path` 再次建立 TCP 连接并下载应用文件. 下载数据写入:

```text
/tmp/environment_monitor.new
```

程序不会直接覆盖正在运行的 `/usr/bin/environment_monitor`.

### 10.3 OTA 安全校验

下载完成后, Qt 程序执行两项校验:

1. 临时文件大小必须等于 manifest 中的 `size`
2. 临时文件 SHA256 必须等于 manifest 中的 `sha256`

同时, manifest 中的应用路径必须以 `/` 开头且不能包含 `..`, 防止下载路径指向不允许的位置.

任意校验失败都会删除 `/tmp/environment_monitor.new`, 保留当前正在运行的旧程序.

### 10.4 替换和回滚

文件:

```text
/home/jvle/Desktop/works/IMX6ULL/porting/environment_monitor_qt/scripts/environment_monitor_ota_apply.sh
```

校验成功后, Qt 使用 `QProcess::startDetached` 启动替换脚本. 脚本执行以下操作:

```text
旧程序 -> /usr/bin/environment_monitor.backup
新程序 -> /usr/bin/environment_monitor
启动新程序
```

如果新程序启动失败, 脚本会使用 `.backup` 文件恢复旧程序. 因此 OTA 更新的是应用文件, 而不是正在运行的 Qt 进程本身.

新程序通过 BusyBox `nohup` 启动, 标准输入连接到 `/dev/null`, 输出保存到:

```text
/tmp/environment_monitor_ota.log
/tmp/environment_monitor_ota_rollback.log
```

这样新程序不会依赖启动 OTA 的串口 shell 会话. 若升级后界面没有出现, 先查看 `/tmp/environment_monitor_ota.log`.

### 10.5 服务器和 rootfs 的关系

`scripts/start_ota_server.sh` 只负责电脑端工作:

1. 使用 Qt ARM 工具链编译程序
2. 生成应用文件和 `manifest.json`
3. 使用 Python HTTP server 提供下载

`mini-rootfs` 负责首次部署所需的文件:

- `/usr/bin/environment_monitor`
- `/usr/bin/environment_monitor_ota_apply.sh`
- Qt 运行库和平台插件

因此第一次必须烧录包含 OTA 功能的完整 rootfs. 后续只要 Qt 程序依赖和替换脚本不变, 就可以通过 WiFi OTA 更新 `/usr/bin/environment_monitor`.
