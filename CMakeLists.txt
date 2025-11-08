set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
cmake_minimum_required(VERSION 3.10)
project(EpsiFrag)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(glslang REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/libs)
include_directories(${CMAKE_SOURCE_DIR}/libs/imgui) 
include_directories(${CMAKE_SOURCE_DIR}/libs/imgui/backends) 
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${glslang_INCLUDE_DIRS})

# ImGui sources
set(IMGUI_SOURCES
    "${CMAKE_SOURCE_DIR}/libs/imgui/imgui.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/imgui_draw.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/imgui_widgets.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/backends/imgui_impl_glfw.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/backends/imgui_impl_vulkan.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/imgui_tables.cpp"
    "${CMAKE_SOURCE_DIR}/libs/imgui/imgui_demo.cpp"
)

# Engine sources (exclude game.cpp if it exists)
file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*quant\\.cpp$")
#list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*game\\.cpp$")


# Main executable with ImGui
add_executable(epsifrag ${ENGINE_SOURCES} ${IMGUI_SOURCES})
target_link_libraries(epsifrag glfw ${Vulkan_LIBRARIES} dl glslang SPIRV)

# Export symbols from the executable for hot-reloaded game module
if(UNIX AND NOT APPLE)
    target_link_options(epsifrag PRIVATE "-Wl,-export-dynamic")
endif()

# Game module as shared library
add_library(game SHARED src/quant.cpp)
# add_library(game SHARED src/game.cpp)
target_link_libraries(game glfw ${Vulkan_LIBRARIES})
set_target_properties(game PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output"
    PREFIX "lib"
)

# Optional: Set RPATH for game module to find dependencies
set_target_properties(game PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)
