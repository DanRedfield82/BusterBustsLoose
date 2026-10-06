cmake_minimum_required(VERSION 3.16)
project(BusterBustsLoosePC C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SDL2_DIR_ROOT ${CMAKE_SOURCE_DIR}/third_party/SDL2-2.32.10)
set(SDL2_INCLUDE ${SDL2_DIR_ROOT}/include)
set(SDL2_LIBDIR ${SDL2_DIR_ROOT}/lib/x64)

if(MSVC)
  # static C runtime: the exe runs without the Visual C++ Redistributable
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W3 /wd4244 /wd4267 /wd4018 /wd4996 /wd4146 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  # the emulation core needs optimisation even in debug builds to reach full speed
  string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
  # keep symbols in release builds so crash reports can be symbolised
  add_compile_options($<$<CONFIG:Release>:/Zi>)
  add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

file(GLOB SNES_CORE_SOURCES ${CMAKE_SOURCE_DIR}/src/snes/*.c)
add_library(snescore STATIC ${SNES_CORE_SOURCES})
target_include_directories(snescore PUBLIC ${CMAKE_SOURCE_DIR}/src/snes)
if(MSVC)
  target_compile_options(snescore PRIVATE $<$<CONFIG:Debug>:/O2 /Ob2>)
endif()

set(GAME_SOURCES
  src/bbl.c
  src/cheats.c
  src/hud.c
  src/filters.c
  src/upscale.c
  src/music.c
  src/levels.c
  src/stb_impl.c
)

add_library(bblgame STATIC ${GAME_SOURCES})
target_include_directories(bblgame PRIVATE ${SDL2_INCLUDE})
target_include_directories(bblgame PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(bblgame PUBLIC snescore)

if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.c)
add_executable(BusterBustsLoose WIN32
  src/main.c
  src/menu.c
  src/config.c
  src/audio.c
  src/finput.c
  src/ui.c
  src/bgtask.c
  src/touch.c
  src/app.rc
)
target_include_directories(BusterBustsLoose PRIVATE ${SDL2_INCLUDE})
target_link_libraries(BusterBustsLoose PRIVATE bblgame ${SDL2_LIBDIR}/SDL2.lib ${SDL2_LIBDIR}/SDL2main.lib comdlg32)
if(MSVC)
  set_target_properties(BusterBustsLoose PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
endif()
add_custom_command(TARGET BusterBustsLoose POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL2_LIBDIR}/SDL2.dll $<TARGET_FILE_DIR:BusterBustsLoose>)
# (SDL2's debug symbols, when the full development package is there)
if(EXISTS ${SDL2_LIBDIR}/SDL2.pdb)
  add_custom_command(TARGET BusterBustsLoose POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL2_LIBDIR}/SDL2.pdb $<TARGET_FILE_DIR:BusterBustsLoose>)
endif()
endif()

# headless developer tool used for testing / reverse engineering
add_executable(bblprobe tools/probe.c)
target_link_libraries(bblprobe PRIVATE bblgame)
