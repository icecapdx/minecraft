cmake_minimum_required(VERSION 3.20)
project(Minecraft CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB PPC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_recomp.*.cpp")

add_executable(MinecraftRuntime
    src/runtime_main.cpp
    src/kernel.cpp
    ppc/ppc_func_mapping.cpp
    ${PPC_SOURCES}
)

target_include_directories(MinecraftRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ppc)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86")
    target_compile_options(MinecraftRuntime PRIVATE -msse4.1 -mavx)
endif()
