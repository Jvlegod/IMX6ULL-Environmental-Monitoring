# IMX6ULL Environment Monitor

This is a Qt Widgets prototype for the IMX6ULL multi-sensor environment monitor. It currently uses a simulated provider so it can be tested on a desktop without hardware.

## Desktop build

Qt 5.12+ or Qt 6 with the Widgets module is required; the trend chart is a lightweight QWidget.

```bash
cmake -S . -B build
cmake --build build
./build/environment_monitor
```

The first version displays temperature, humidity, pressure, illuminance, trend lines, threshold alerts and connection states for BMP280, RS485, VEML7700 and serial WiFi.

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
- Serial WiFi: add the confirmed serial or TCP transport and report link state.

References:

- https://doc.qt.io/qt-5/qtwidgets-index.html
- https://doc.qt.io/qt-5/qtcharts-index.html
- https://doc.qt.io/qt-5/cmake-get-started.html
- https://www.bosch-sensortec.com/products/environmental-sensors/pressure-sensors/bmp280/
