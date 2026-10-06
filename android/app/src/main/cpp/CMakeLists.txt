# Native part of the Android build: SDL2 from source (libSDL2.so) and the port
# itself (libmain.so, loaded by SDLActivity, which calls its SDL_main).
cmake_minimum_required(VERSION 3.18)
project(BusterBustsLooseAndroid C)

get_filename_component(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ABSOLUTE)
set(SDL_DIR ${ROOT}/third_party/SDL2-2.32.10-src)
if(NOT EXISTS ${SDL_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "SDL2 source not found in ${SDL_DIR}: run android/fetch_sdl.ps1 first")
endif()

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL_DIR} ${CMAKE_BINARY_DIR}/SDL)

file(GLOB SNES_CORE ${ROOT}/src/snes/*.c)
add_library(main SHARED
  ${SNES_CORE}
  ${ROOT}/src/bbl.c
  ${ROOT}/src/cheats.c
  ${ROOT}/src/hud.c
  ${ROOT}/src/filters.c
  ${ROOT}/src/upscale.c
  ${ROOT}/src/music.c
  ${ROOT}/src/levels.c
  ${ROOT}/src/stb_impl.c
  ${ROOT}/src/main.c
  ${ROOT}/src/menu.c
  ${ROOT}/src/config.c
  ${ROOT}/src/audio.c
  ${ROOT}/src/finput.c
  ${ROOT}/src/ui.c
  ${ROOT}/src/bgtask.c
  ${ROOT}/src/touch.c
)
set_target_properties(main PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(main PRIVATE ${ROOT}/src ${ROOT}/src/snes ${ROOT}/third_party/stb)
# (the emulation core needs optimising even in debug builds to reach full speed)
target_compile_options(main PRIVATE -O2 -Wno-unused-parameter -Wno-sign-compare)
target_link_libraries(main PRIVATE SDL2 android log m)
