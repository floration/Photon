cmake_minimum_required(VERSION 3.20)
project(Photon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# AVX2 is assumed on every supported CPU (handoff: hardware baseline 2015+).
if(MSVC)
  add_compile_options(/W3 /arch:AVX2 /fp:precise)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -mavx2 -mfma -fno-math-errno)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

# ---- core: tiny shared utilities (job system, math) -------------------------
add_library(photon_core INTERFACE)
target_include_directories(photon_core INTERFACE src)
find_package(Threads REQUIRED)
target_link_libraries(photon_core INTERFACE Threads::Threads)

# ---- scene: scene file, material/assembly data, framing expansion -----------
add_library(photon_scene STATIC
  src/scene/textfile.cpp
  src/scene/materials.cpp
  src/scene/scene.cpp)
target_link_libraries(photon_scene PUBLIC photon_core)

# ---- sim: solver core. Must not depend on platform or render. ---------------
add_library(photon_sim STATIC
  src/sim/discretize.cpp
  src/sim/solver.cpp)
target_link_libraries(photon_sim PUBLIC photon_scene)

# ---- platform: thin OS layer (Win32 now, X11 in Phase 2) --------------------
if(WIN32)
  add_library(photon_platform STATIC src/platform/platform_win32.cpp)
  target_link_libraries(photon_platform PUBLIC photon_core opengl32 gdi32 user32)
else()
  message(FATAL_ERROR "Phase 1 prototype targets Windows; the Linux platform layer lands in Phase 2.")
endif()

# ---- render: OpenGL 3.3 core renderer -------------------------------------
add_library(photon_render STATIC
  src/render/gl.cpp
  src/render/renderer.cpp)
target_link_libraries(photon_render PUBLIC photon_platform)

# ---- app: viewer executable -------------------------------------------------
add_executable(photon WIN32 src/app/main.cpp src/app/viewer.cpp)
target_link_libraries(photon PRIVATE photon_sim photon_render photon_platform)
if(MINGW)
  target_link_options(photon PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ---- headless runner: proves the solver builds without a window -------------
add_executable(photon_cli src/app/cli.cpp)
target_link_libraries(photon_cli PRIVATE photon_sim)
if(MINGW)
  target_link_options(photon_cli PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Copy data next to the executables so they run from the build folder.
foreach(t photon photon_cli)
  add_custom_command(TARGET ${t} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:${t}>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/scenes $<TARGET_FILE_DIR:${t}>/scenes)
endforeach()
