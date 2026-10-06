cmake_minimum_required(VERSION 3.24)
project(RickyRoseNative LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CUDA_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# RTX 5090 is sm_120 (Blackwell). GitHub's Windows runners have no GPU,
# so the architecture must be named explicitly rather than detected.
set(CMAKE_CUDA_ARCHITECTURES 120)
set(CMAKE_CUDA_RUNTIME_LIBRARY Static)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Defines the CUDA:: imported targets (cudart_static below).
find_package(CUDAToolkit REQUIRED)

include(FetchContent)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.5)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

add_executable(RickyRose WIN32
  src/main.cpp
  src/audio.cpp
  src/exports.cpp
  src/glprocs.cpp
  src/render.cu)
set_target_properties(RickyRose PROPERTIES
  OUTPUT_NAME "Reverend Ricky Rosè's Dance Party Visuals"
  CUDA_SEPARABLE_COMPILATION OFF)
target_include_directories(RickyRose PRIVATE src)
target_link_libraries(RickyRose PRIVATE imgui glfw opengl32 ole32 uuid comdlg32 shell32 CUDA::cudart_static)
target_compile_options(RickyRose PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W3 /permissive->
  $<$<COMPILE_LANGUAGE:CUDA>:-use_fast_math --ptxas-options=-v>)
