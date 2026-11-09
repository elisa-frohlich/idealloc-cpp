cmake_minimum_required(VERSION 3.14)
project(Idealloc VERSION 1.0 LANGUAGES CXX)

# Force C++20 or higher
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 1. Idealloc Library Target
# ==========================================

add_library(idealloc-lib
    lib/Instance.cpp
    lib/Job.cpp
    lib/Jobset.cpp
    lib/Helper.cpp
    lib/Analyzer.cpp
    lib/algo/Boxing.cpp
    lib/algo/Placement.cpp
    lib/algo/Mod.cpp    
)

target_include_directories(idealloc-lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(idealloc-lib PUBLIC cxx_std_20)

add_library(idealloc-lib::idealloc-lib ALIAS idealloc-lib)


# ==========================================
# 2. Main Application Target
# ==========================================

add_executable(idealloc
    src/main.cpp
)

target_link_libraries(idealloc PRIVATE idealloc-lib)