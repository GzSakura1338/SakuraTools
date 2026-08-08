cmake_minimum_required(VERSION 3.21)
project(MinecraftInjectedProxy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(PROXY_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/native")

add_library(MinecraftProxy_msvc SHARED
    native/ReflectiveLoader.c
    native/loader.cpp
    native/env.cpp
    native/random_name.cpp
    native/classfile.cpp
    native/class_edit.cpp
    native/trampolines.cpp
    native/relay_handler.cpp
    native/connection_hook.cpp
    native/world_cache.cpp
    native/b_server.cpp
)

target_include_directories(MinecraftProxy_msvc PRIVATE
    "${PROXY_NATIVE_DIR}"
    "${PROXY_NATIVE_DIR}/include"
)
target_compile_definitions(MinecraftProxy_msvc PRIVATE
    REFLECTIVEDLLINJECTION_CUSTOM_DLLMAIN
    WIN_X64
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(MinecraftProxy_msvc PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
    $<$<CONFIG:Release>:/O2>
)
target_link_libraries(MinecraftProxy_msvc PRIVATE
    psapi
    ws2_32
    kernel32
    user32
    advapi32
)
set_target_properties(MinecraftProxy_msvc PROPERTIES
    OUTPUT_NAME "MinecraftProxy_msvc"
)

add_executable(reflective_injector
    injector/cli.c
    injector/Inject.c
    injector/LoadLibraryR.c
    injector/GetProcAddressR.c
)
target_include_directories(reflective_injector PRIVATE injector)
target_compile_definitions(reflective_injector PRIVATE
    REFLECTIVEDLLINJECTION_CUSTOM_DLLMAIN
    WIN_X64
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(reflective_injector PRIVATE
    advapi32
    iphlpapi
    kernel32
    user32
)

add_library(starain_inject SHARED
    native/jni_inject.cpp
    injector/Inject.c
    injector/LoadLibraryR.c
    injector/GetProcAddressR.c
)
target_include_directories(starain_inject PRIVATE
    "${PROXY_NATIVE_DIR}/include"
    injector
)
target_compile_definitions(starain_inject PRIVATE
    REFLECTIVEDLLINJECTION_CUSTOM_DLLMAIN
    WIN_X64
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(starain_inject PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
    $<$<CONFIG:Release>:/O2>
)
target_link_libraries(starain_inject PRIVATE
    kernel32
    user32
)
set_target_properties(starain_inject PROPERTIES
    OUTPUT_NAME "starain_inject"
)
