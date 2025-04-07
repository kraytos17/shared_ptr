cmake_minimum_required(VERSION 3.30)
project(sp
  VERSION 0.1.0
  DESCRIPTION "My own shared_ptr impl"
  LANGUAGES CXX
)

find_program(CLANG_EXE NAMES "clang++")
if(CLANG_EXE)
    message(STATUS "Clang++ found: ${CLANG_EXE}")
    set(CMAKE_CXX_COMPILER "${CLANG_EXE}" CACHE STRING "C++ compiler")
else()
    message(STATUS "Clang++ not found, using system default compiler")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_DEBUG_POSTFIX "-d")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(compiler_options INTERFACE)

target_compile_options(compiler_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wcast-align
    -Wunused
    -Wmisleading-indentation
    -Wno-gnu-zero-variadic-macro-arguments
)

# Debug configuration
target_compile_options(compiler_options INTERFACE
    $<$<CONFIG:Debug>:
        -O0
        -ggdb
        -g3
        -DDEBUG
        -fno-omit-frame-pointer
        -fno-inline
        -fno-optimize-sibling-calls
        -fstrict-aliasing
        -fstack-protector-strong
        -fsanitize=address,undefined
    >
)

# Release configuration
target_compile_options(compiler_options INTERFACE
    $<$<CONFIG:Release>:
        -O3
        -DNDEBUG
        -flto
        -march=native
        -fno-exceptions
        -fno-rtti
    >
)

# Linker options per config
target_link_options(compiler_options INTERFACE
    $<$<CONFIG:Debug>:
        -fsanitize=address,undefined
    >
    $<$<CONFIG:Release>:
        -flto
    >
)

# Detect compiler and set specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Using Clang compiler")
    target_compile_options(compiler_options INTERFACE
        -ferror-limit=100
        -Wno-c++98-compat
        -Wno-padded
        -Wno-documentation-unknown-command
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Using GCC compiler")
    target_compile_options(compiler_options INTERFACE
        -fmax-errors=100
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
    )
endif()

find_program(LLD_LINKER NAMES ld.lld lld)
if(LLD_LINKER)
    message(STATUS "LLD linker found: ${LLD_LINKER}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Using ccache")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Build target
add_executable(spt src/sp_test.cpp)
target_link_libraries(spt PRIVATE compiler_options)

# Testing
include(CTest)
enable_testing()
add_test(NAME spt COMMAND spt)

# Packaging
set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
