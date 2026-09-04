# IMX6ULL Environment Monitor

This is a Qt Widgets prototype for the IMX6ULL multi-sensor environment monitor. It currently uses a simulated provider so it can be tested on a desktop without hardware.

## Desktop build

Qt 5.12+ or Qt 6 with the Widgets module is required; the trend chart is a lightweight QWidget.

```bash
cmake -S . -B build
cmake --build build
./build/environment_monitor
```

The current version displays temperature, humidity, pressure, illuminance, trend lines, configurable sampling intervals, configurable threshold alerts and connection states for BMP280, RS485, VEML7700 and serial WiFi. Abnormal-data reporting currently appears on screen; audio reporting is reserved for a later version.

## IMX6ULL deployment

Use the vendor Qt 5.12.9 ARM kit and the existing mini-rootfs Qt runtime script:

```bash
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=/path/to/qt-toolchain.cmake
cmake --build build-arm
cd ../mini-rootfs
./scripts/install_qt_runtime.sh
./scripts/install_binary.sh ../environment_monitor_qt/build-arm/environment_monitor /usr/bin
./scripts/build_rootfs.sh --reuse-busybox
```

On the board, launch with the linuxfb platform plugin if it is not already configured:

```sh
export QT_QPA_PLATFORM=linuxfb
/usr/bin/environment_monitor
```

## Hardware integration

`ISensorProvider` is the stable boundary for hardware access. Implement a provider that emits `SensorSnapshot` and `deviceStatusChanged`, then construct it in `main.cpp` instead of `SimulatedSensorProvider`.

- BMP280: read the existing Linux IIO files such as `in_pressure_input` and `in_temp_input`.
- RS485: add the confirmed Modbus RTU or custom-frame parser.
- VEML7700: add an I2C reader after its bus address and kernel access path are confirmed.
- Serial WiFi: use the built-in ESP8266 configuration dialog to open the UART, scan nearby APs and join a network.

## Sampling and threshold configuration

The dashboard provides sampling intervals of 1, 5, 10, 30 and 60 seconds. The selected interval is applied to the provider timer and persisted with `QSettings`.

The threshold dialog supports minimum and maximum values for temperature, humidity, pressure and illuminance. A snapshot is marked abnormal when a value is outside its configured range or is not finite. The current reporting action is the on-screen alert banner only.

On the embedded board, settings use a stable path that is not replaced by application OTA:

```text
/etc/environment_monitor/settings.ini
```

Desktop builds use the standard writable application configuration directory. The first run of the new path migrates the previous `~/.config/jvle/environment_monitor.conf` when it exists.

## Independent acquisition tasks

The `采集任务` control supports selecting BMP280, RS485 temperature/humidity and VEML7700 independently. A task can run for a specified duration or between an absolute start and end time. Samples from selected devices are appended to a CSV file; unselected device fields are not written as measurements.

The current simulated provider implements this selection through `ISensorProvider::setEnabledDevices`. A hardware provider should use the same mask to read only the requested sub-devices and emit a `SensorSnapshot`. The current task output is CSV rather than SQLite.

The CSV contains an ISO timestamp and columns for the selected devices, for example:

```text
timestamp,bmp280_temperature,bmp280_pressure,rs485_humidity
```

When a task starts, the provider uses the selected device mask. When it finishes or is stopped, the provider returns to all devices. Abnormal-data reporting remains on-screen only; audio reporting is reserved for a later version.

## ESP8266 WiFi setup

Connect the ATK-MW8266D UART TX to the IMX6ULL UART RX, UART RX to UART TX, and share GND. The module accepts a 3.3 V to 5 V supply, while its UART uses 3.3 V LVTTL levels.

1. Start the application and select `WiFi 配置`.
2. The dialog automatically selects the first available board UART, opens it with the module default `115200 8N1` and starts scanning.
3. Double-click the target SSID, enter its password and connect. Use `重新扫描 WiFi` when another scan is needed.
4. If the module does not answer `AT`, verify the crossed TX/RX wiring, common ground, power capacity, UART device permissions and firmware baud rate.

The controller follows the supplied `ESP8266_AT指令集V2.1.0.pdf`: `AT`, `AT+CWMODE_CUR=1`, `AT+CWLAP`, `AT+CWJAP` and `AT+CIFSR`.

References:

- https://doc.qt.io/qt-5/qtwidgets-index.html
- https://doc.qt.io/qt-5/qtcharts-index.html
- https://doc.qt.io/qt-5/cmake-get-started.html
- https://www.bosch-sensortec.com/products/environmental-sensors/pressure-sensors/bmp280/
- https://docs.espressif.com/projects/esp-at/en/latest/esp8266/AT_Command_Set/Wi-Fi_AT_Commands.html
- `../../【正点原子】WIFI模块ATK-ESP8266资料（新资料）/4，参考资料/ESP8266_AT指令集V2.1.0.pdf`
- `../../【正点原子】WIFI模块ATK-ESP8266资料（新资料）/ATK-MW8266D模块用户手册_V1.3.pdf`

## WiFi OTA application update

The WiFi dialog can download and install a Qt application update from a computer on the same LAN. The ESP8266 must already be connected to the target WiFi network. The first version uses HTTP plus manifest SHA256 verification and keeps a `.backup` copy during replacement.

Build an ARM application and prepare a server directory:

```sh
mkdir -p ota
cp build-arm/environment_monitor ota/environment_monitor
sha256sum ota/environment_monitor
stat -c '%s' ota/environment_monitor
python3 scripts/make_ota_manifest.py --file ota/environment_monitor --version 0.2.0 --path /environment_monitor --output ota/manifest.json
cd ota && python3 -m http.server 8080
```

For one-command ARM build, manifest generation and HTTP server startup, run:

```sh
./scripts/start_ota_server.sh -v 0.2.0
```

The script uses the local Qt ARM SDK, copies the generated `environment_monitor` into `ota/`, generates `manifest.json`, prints the computer LAN address and starts the server on port `8080`. The development board only needs to connect to WiFi and click `检查并升级应用`.

In `WiFi 配置`, set the computer IP, keep port `8080` and manifest path `/manifest.json`, then click `检查并升级应用`. The rootfs install must include `environment_monitor_ota_apply.sh`; the CMake install rule installs it to `/usr/bin`.
