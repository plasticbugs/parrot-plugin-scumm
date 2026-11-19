# SCUMM: SCUMM games on the ColecoVision, Monkey Island first. Pick a
# game's 000.LFL in the menu and press A: ScummVM's SCUMM engine (vendored
# from frank-quest) runs on the cart, and scummcv + nescv send the console
# each picture and the PCjr music as the Game Boy kernel's lists.
#
# The engine is ~900 KB: more than SRAM holds, so this is a flash binary,
# run in place (XIP) from its slot.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                      -> build/SCUMM.CVP
#
# Copy SCUMM.CVP into the card's /PLUGINS folder.
cmake_minimum_required(VERSION 3.13)

# the cart's SDK: the copy in sdk/ (sdk/README.md)
set(PARROTCART_SDK ${CMAKE_CURRENT_LIST_DIR}/sdk/firmware/revc)
set(PICO_BOARD_HEADER_DIRS ${PARROTCART_SDK}/cvcart_revc/boards)
set(PICO_BOARD cvcart_revc)
set(PICO_PLATFORM rp2350-arm-s)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/cmake/pico_sdk_import.cmake)

project(scumm C CXX ASM)
pico_sdk_init()
include(${PARROTCART_SDK}/sdk/cvplugin.cmake)

# The same source makes the same file, whenever it's built: the compiler's
# __DATE__ and __TIME__ (ScummVM's build date, in the binary) come from
# SOURCE_DATE_EPOCH, fixed here at the released build's,
# 2026-11-18 20:00:00 UTC.
set(BUILD_EPOCH 1795032000)
set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})

set(V ${CMAKE_CURRENT_LIST_DIR}/vendor/frank-quest)
# the engine: vendor/frank-quest-sources.txt, and the cart's file system
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/vendor/frank-quest-sources.txt lines)
set(ENGINE_SRC)
foreach(l ${lines})
  if(l MATCHES "^src/" AND NOT l MATCHES "\\[host\\]")
    list(APPEND ENGINE_SRC ${V}/${l})
  endif()
endforeach()
list(APPEND ENGINE_SRC
  ${V}/src/backends/fs/abstract-fs.cpp
  ${V}/src/backends/fs/rp2350/rp2350-fs.cpp
  ${V}/src/backends/fs/rp2350/rp2350-fs-factory.cpp)
set(FATFS_SRC ${V}/src/fatfs/ff.c ${V}/src/fatfs/ffsystem.c ${V}/src/fatfs/ffunicode.c ${V}/src/frank_quest_fs.c)
# vendored: its warnings aren't ours; -Os keeps it in its flash slot
set_source_files_properties(${ENGINE_SRC} ${FATFS_SRC} PROPERTIES COMPILE_OPTIONS "-w;-Os")
set_source_files_properties(src/cart.cpp src/psgmix.cpp src/scummconf.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wno-register")
set_source_files_properties(src/diskio.c src/heap.c src/usbinput.c src/usbmidi_dev.c src/scummcv.c src/savemenu.c src/nescv.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

add_executable(scumm src/cart.cpp src/psgmix.cpp src/scummconf.cpp src/usbinput.c src/usbmidi_dev.c src/diskio.c src/heap.c src/scummcv.c src/savemenu.c src/nescv.c
               ${PARROTCART_SDK}/midihello/vdp.c ${ENGINE_SRC} ${FATFS_SRC})
# src/ first: its tusb_config.h
target_include_directories(scumm PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src ${PARROTCART_SDK}/midihello)
target_include_directories(scumm SYSTEM PRIVATE ${V}/src ${V}/src/engines)
# frank-quest's definitions (its CMakeLists.txt), SCUMM only
target_compile_definitions(scumm PRIVATE __RP2350__=1 CABAL_HAVE_STDINT_H=1 ENABLE_SCUMM=1
  CABAL_LOW_MEMORY=1 DISABLE_GUI=1 DISABLE_DEFAULT_AUDIOCD_MANAGER=1 CABAL_MINIMAL_AUDIO=1
  CABAL_SCREEN_WIDTH=320 CABAL_SCREEN_HEIGHT=200 CABAL_VERSION="frank-quest-949301b"
  PICO_MALLOC_PANIC=0)
# The engine's error messages name their source file (__FILE__): as a path
# from here, so the build is the same wherever the repository is.
target_compile_options(scumm PRIVATE -fmacro-prefix-map=${CMAKE_CURRENT_LIST_DIR}/=)
cvframes_use(scumm)
cvframes_serve2(scumm)   # core 1's bus loop acts on two samples of the pins that agree (sdk/cvfserve.S)
cvsd_use(scumm)
cvpsram_use(scumm)
# a USB keyboard and mouse (usbinput.c). TinyUSB 0.18's hub driver asks for
# the hub descriptor with wValue 0, which a VIA VL817 hub stalls: the SDK's
# fixed copy instead.
target_link_libraries(scumm tinyusb_host tinyusb_device)   # one role at a time (usbmidi_dev.h)
target_sources(scumm PRIVATE ${PARROTCART_SDK}/midihello/tinyusb_fix/hub.c)
set_source_files_properties(${PICO_TINYUSB_PATH}/src/host/hub.c
    TARGET_DIRECTORY scumm PROPERTIES HEADER_FILE_ONLY TRUE)
pico_enable_stdio_usb(scumm 0)
pico_enable_stdio_uart(scumm 0)
cvcart_plugin(scumm ID parrotcart.scumm NAME "SCUMM" VERSION 0.2.9 BY "Parrot Cart"
              HOME /SCUMM OPENS .lfl .000 ICON src/scumm_icon.txt FILE_ICON src/scumm_file_icon.txt XIP
              README CART-README.md)
