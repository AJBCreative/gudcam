cmake_minimum_required(VERSION 3.10)
project(gudcam CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags for high performance Linux systems
add_compile_options(-O3 -march=native -ffast-math -pthread -Wall -Wextra)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(SOURCES
    src/v4l2_engine.cpp
    src/v4l2_controls.cpp
    src/gpu_pipeline.cpp
    src/c_api.cpp
)

# 1. Build C ABI Shared Library libgudcam.so
add_library(gudcam SHARED ${SOURCES})
target_link_libraries(gudcam PRIVATE pthread)

# 2. Build Native C++ Standalone Binary
add_executable(gudcam_app src/main.cpp ${SOURCES})
target_link_libraries(gudcam_app PRIVATE pthread)

# 3. Build Test Suites
add_executable(test_lockfree tests/test_lockfree.cpp src/gpu_pipeline.cpp)
target_link_libraries(test_lockfree PRIVATE pthread)

add_executable(test_v4l2 tests/test_v4l2.cpp ${SOURCES})
target_link_libraries(test_v4l2 PRIVATE pthread)
