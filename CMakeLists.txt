cmake_minimum_required(VERSION 3.30)
project(sp
  VERSION 0.1.0
  DESCRIPTION "My own shared_ptr impl"
  LANGUAGES CXX
)

set(CMAKE_CXX_COMPILER "clang++" CACHE STRING "C++ compiler")
find_program(CLANG_EXE NAMES "clang++")
if(NOT CLANG_EXE)
    message(STATUS "Clang++ not found, falling back to default compiler")
    unset(CMAKE_CXX_COMPILER CACHE)
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
    -Werror
    -pedantic
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Using Clang compiler")
    target_compile_options(compiler_options INTERFACE
        -ferror-limit=100
        -Weverything
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

target_compile_options(compiler_options INTERFACE
    $<$<CONFIG:Debug>:
        -O0
        -g3
        -DDEBUG
        -fno-omit-frame-pointer
        -fno-inline
        -fsanitize=address,undefined
        -fno-optimize-sibling-calls
    >
)

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

target_link_options(compiler_options INTERFACE
    $<$<CONFIG:Debug>:
        -fsanitize=address,undefined
    >
    $<$<CONFIG:Release>:
        -flto
    >
)

include(CTest)
enable_testing()

add_executable(spt src/sp_test.cpp)
target_link_libraries(spt PRIVATE compiler_options)
add_test(NAME spt COMMAND spt)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
