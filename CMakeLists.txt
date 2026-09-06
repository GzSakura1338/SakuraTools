cmake_minimum_required(VERSION 3.21)
project(MinecraftInjectedProxy LANGUAGES C CXX)

# build.ps1 detects the native compiler's localized prefix without CMake's
# console-code-page conversion. This also keeps Ninja's header dependencies valid.
if(MSVC AND DEFINED PROXY_MSVC_INCLUDE_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${PROXY_MSVC_INCLUDE_PREFIX}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT WIN32)
    message(FATAL_ERROR "The DLL targets Windows. For Zig cross-compilation use scripts/build.sh.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use an x64 compiler: the reflective loader targets 64-bit Minecraft.")
endif()

# Keep all production entry points on the same source list.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS native/sources.txt)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/native/sources.txt" PROXY_SOURCES)

add_library(proxy_build_options INTERFACE)
target_compile_definitions(proxy_build_options INTERFACE
    REFLECTIVEDLLINJECTION_CUSTOM_DLLMAIN WIN_X64
    NOMINMAX _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(proxy_build_options INTERFACE /utf-8
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<CONFIG:Release>:/O2>)
endif()

# Meadow: resident DLL inside client A.
add_library(MinecraftProxy_msvc SHARED ${PROXY_SOURCES})
target_include_directories(MinecraftProxy_msvc PRIVATE native native/include)
target_link_libraries(MinecraftProxy_msvc PRIVATE
    proxy_build_options psapi ws2_32 kernel32 user32 advapi32)
set_target_properties(MinecraftProxy_msvc PROPERTIES OUTPUT_NAME Meadow)

# Canvas and Ribbon share the same injection implementation.
set(INJECTION_SOURCES
    injector/Inject.c
    injector/LoadLibraryR.c
    injector/GetProcAddressR.c)

add_executable(reflective_injector injector/cli.c ${INJECTION_SOURCES})
target_include_directories(reflective_injector PRIVATE injector)
target_link_libraries(reflective_injector PRIVATE
    proxy_build_options advapi32 iphlpapi kernel32 user32)
set_target_properties(reflective_injector PROPERTIES OUTPUT_NAME Canvas)

add_library(starain_inject SHARED native/jni_inject.cpp ${INJECTION_SOURCES})
target_include_directories(starain_inject PRIVATE native/include injector)
target_link_libraries(starain_inject PRIVATE proxy_build_options kernel32 user32)
set_target_properties(starain_inject PROPERTIES OUTPUT_NAME Ribbon)

set(PROXY_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proxy")
foreach(target MinecraftProxy_msvc reflective_injector starain_inject)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROXY_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PROXY_OUTPUT_DIR}")
    # Visual Studio and other multi-config generators must not append Debug/Release.
    foreach(config Debug Release RelWithDebInfo MinSizeRel ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${config}" config_upper)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${PROXY_OUTPUT_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${PROXY_OUTPUT_DIR}")
    endforeach()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
