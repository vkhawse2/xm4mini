cmake_minimum_required(VERSION 3.20)
project(xm4mini VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Vendored Sound-connect transport + V1 protocol (see VENDOR.md) ---
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

set(VENDOR_SRCS
    ${VENDOR_DIR}/Client/windows/WindowsBluetoothConnector.cpp
    ${VENDOR_DIR}/Client/ByteMagic.cpp
    ${VENDOR_DIR}/libs/sony-transport/src/BluetoothConnectorTransport.cpp
    ${VENDOR_DIR}/libs/sony-transport/src/Logger.cpp
    ${VENDOR_DIR}/libs/sony-transport/src/SonyDeviceFilter.cpp
    ${VENDOR_DIR}/libs/sony-protocol/src/FrameCodec.cpp
    ${VENDOR_DIR}/libs/sony-protocol/src/SonyProtocolSession.cpp
    ${VENDOR_DIR}/libs/sony-protocol/src/ProtocolV1.cpp
    ${VENDOR_DIR}/libs/sony-protocol/src/EqualizerPresets.cpp
    ${VENDOR_DIR}/libs/sony-protocol/src/DeviceProfileRegistry.cpp
)

# The resource compiler resolves ICON paths against the build dir.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/xm4mini.ico
               ${CMAKE_CURRENT_BINARY_DIR}/xm4mini.ico COPYONLY)

add_executable(xm4mini WIN32
    src/main.cpp
    src/ui.cpp
    src/device_link.cpp
    src/battery_est.cpp
    src/settings.cpp
    src/resources.rc
    ${VENDOR_SRCS}
)

target_include_directories(xm4mini PRIVATE
    src
    ${VENDOR_DIR}/Client
    ${VENDOR_DIR}/Client/windows
    ${VENDOR_DIR}/libs/sony-transport/include
    ${VENDOR_DIR}/libs/sony-transport/src
    ${VENDOR_DIR}/libs/sony-protocol/include
    ${VENDOR_DIR}/libs/sony-protocol/src
)

target_compile_definitions(xm4mini PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(xm4mini PRIVATE
    ws2_32
    rpcrt4
    Bthprops
    gdiplus
    shell32
    dwmapi
    ole32
)

if(MSVC)
    target_compile_options(xm4mini PRIVATE /W4 /EHsc /utf-8)
    # Tiny binary: no RTTI needed, favor size.
    target_compile_options(xm4mini PRIVATE /GR-)
endif()
