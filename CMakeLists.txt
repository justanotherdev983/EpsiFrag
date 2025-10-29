set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
cmake_minimum_required(VERSION 3.10)

project(EpsiFrag)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
include_directories(${CMAKE_SOURCE_DIR}/libs)
include_directories(${CMAKE_SOURCE_DIR}/libs/imgui) 
include_directories(${CMAKE_SOURCE_DIR}/libs/imgui/backends) 
include_directories(${CMAKE_SOURCE_DIR}/include)


file(GLOB_RECURSE SOURCES
    "src/*.cpp"
)

list(APPEND SOURCES
    "${CMAKE_SOURCE_DIR}/libs/imgui/imgui.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/imgui_draw.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/imgui_widgets.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/backends/imgui_impl_glfw.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/backends/imgui_impl_vulkan.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/imgui_tables.cpp"
     "${CMAKE_SOURCE_DIR}/libs/imgui/imgui_demo.cpp"
 
)

add_executable(epsifrag ${SOURCES})

target_link_libraries(epsifrag glfw ${Vulkan_LIBRARIES})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g3 ")
