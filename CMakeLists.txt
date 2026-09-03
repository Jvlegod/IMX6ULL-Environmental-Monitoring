cmake_minimum_required(VERSION 3.10)
project(environment_monitor_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if (Qt6_FOUND)
    set(QT_MAJOR 6)
else()
    find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets)
    set(QT_MAJOR 5)
endif()

add_executable(environment_monitor
    main.cpp
    sensorprovider.cpp
    sensorprovider.h
    mainwindow.cpp
    mainwindow.h
    trendchart.cpp
    trendchart.h
    esp8266controller.cpp
    esp8266controller.h
    wifidialog.cpp
    wifidialog.h
)

if (QT_MAJOR EQUAL 6)
    target_link_libraries(environment_monitor PRIVATE Qt6::Widgets)
else()
    target_link_libraries(environment_monitor PRIVATE Qt5::Widgets)
endif()

if (UNIX AND NOT APPLE)
    install(TARGETS environment_monitor RUNTIME DESTINATION bin)
endif()
