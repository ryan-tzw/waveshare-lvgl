# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# define proj name, and specifies languages: c, c++ and asm
project(RP2350-Touch-LCD-2 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add compilation subdirectory (searches for other CMakeLists.txt files)
add_subdirectory(./examples)
add_subdirectory(./lib/Config)
add_subdirectory(./lib/LCD)
add_subdirectory(./lib/Touch)
add_subdirectory(./lib/QMI8658)
add_subdirectory(./lib/lvgl)

# Add header file directory (where to search for header files when using #include)
include_directories(./src)
include_directories(./lib/Config)
include_directories(./lib/LCD)
include_directories(./lib/Touch)
include_directories(./lib/QMI8658)
include_directories(./lib/lvgl)

aux_source_directory(./src SRC)

# PICO_CONFIG: PICO_XOSC_STARTUP_DELAY_MULTIPLIER, 
# Multiplier to lengthen xosc startup delay to accommodate slow-starting oscillators, 
# type=int, min=1, default=1, group=hardware_xosc

add_executable(RP2350-Touch-LCD-2 main.c ${SRC})

pico_set_program_name(RP2350-Touch-LCD-2 "RP2350-Touch-LCD-2")
pico_set_program_version(RP2350-Touch-LCD-2 "0.1")

pico_enable_stdio_uart(RP2350-Touch-LCD-2 0)
pico_enable_stdio_usb(RP2350-Touch-LCD-2 1)

# Add the standard library to the build

set(  LV_CONF_PATH
      ${CMAKE_CURRENT_SOURCE_DIR}/lib/LVGL/lv_conf.h
      CACHE STRING "" FORCE
)

add_compile_options(-O2)

target_link_libraries(  RP2350-Touch-LCD-2 
                        LCD 
                        Touch
                        QMI8658
                        LVGL
                        Config
                        examples
                        pico_stdlib
                        hardware_spi
                        hardware_i2c
                        hardware_dma
)

# Add the standard include files to the build
target_include_directories(RP2350-Touch-LCD-2 PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(RP2350-Touch-LCD-2)

