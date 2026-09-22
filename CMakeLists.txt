cmake_minimum_required(VERSION 3.14)
project(Idealloc VERSION 1.0 LANGUAGES CXX)

# Force C++17 or higher
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 1. Main Application Target
# ==========================================

# Executable for the main static analyzer tool
add_executable(idealloc
    src/main.cpp
    lib/Instance.cpp
)

# Specify include directories directly on the target
target_include_directories(idealloc PRIVATE include)
