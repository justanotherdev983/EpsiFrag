# EpsiFrag
A simple multiplayer concept from scratch with vulkan SDK

## Prerequisites

- CMake 3.10+
- Clang compiler
- GLFW3
- Vulkan SDK

## Building

### 1. Compile Shaders

```bash
cd src/shaders
./compile.sh  # or manually compile with glslc
cd ../..
```

### 2. Build the Project

```bash
mkdir -p build
cd build
cmake ..
make
```

### 3. Run

```bash
./epsifrag
```

## Troubleshooting

### Validation Layers Not Found

If you get the error:
```
[CANDY ASSERT FAILED] Validation layers not available
```

Make sure you've exported the `VK_LAYER_PATH` environment variable correctly. You can verify it's set:

```bash
echo $VK_LAYER_PATH
ls $VK_LAYER_PATH  # Should show VkLayer_khronos_validation.json
```

**Alternative:** Install system-wide validation layers:
```bash
sudo apt install vulkan-validationlayers
```

### Disabling Validation Layers (Release Builds)

For release builds, validation layers are automatically disabled. To disable them in debug builds, change in `src/main.cpp`:

```cpp
#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION = false;
#else
constexpr bool ENABLE_VALIDATION = false;  // Set to false to disable
#endif
```

## Project Structure

```
EpsiFrag/
├── src/
│   ├── main.cpp              # Main renderer code
│   └── shaders/              # GLSL shaders
│       ├── simple_shader.vert
│       ├── simple_shader.frag
│       └── compile.sh
├── libs/
│   └── vulkansdk/            # Local Vulkan SDK
└── build/                    # Build output
```

## Features

- Custom Vulkan renderer ("Candy Engine")
- Validation layer support for debugging
- Swapchain management
- Graphics pipeline setup

## Cloc CMD
```bash
cloc --exclude-dir=build,libs .
```
