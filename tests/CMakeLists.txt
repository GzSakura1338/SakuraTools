set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

function(add_native_test name source)
    add_executable(${name}_test ${source} ${ARGN})
    target_include_directories(${name}_test PRIVATE "${PROJECT_SOURCE_DIR}/native")
    if(MSVC)
        target_compile_options(${name}_test PRIVATE /utf-8)
    endif()
    add_test(NAME ${name} COMMAND ${name}_test)
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

add_native_test(login_handoff login_handoff_test.cpp)
add_native_test(runtime_gate runtime_gate_test.cpp)
add_native_test(packet_policy packet_policy_test.cpp)
add_native_test(team_state team_state_test.cpp)
add_native_test(class_editor edit_self_test.cpp
    "${PROJECT_SOURCE_DIR}/native/classfile.cpp"
    "${PROJECT_SOURCE_DIR}/native/class_edit.cpp")

# Requires local Minecraft jars, so invoked separately by test_snapshot_native.ps1.
add_executable(snapshot_jni_test snapshot_jni_test.cpp)
target_include_directories(snapshot_jni_test PRIVATE "${PROJECT_SOURCE_DIR}/native")

add_executable(server_jni_test server_jni_test.cpp
    "${PROJECT_SOURCE_DIR}/native/jni_lookup.cpp"
    "${PROJECT_SOURCE_DIR}/native/status_response.cpp"
    "${PROJECT_SOURCE_DIR}/native/server/state.cpp"
    "${PROJECT_SOURCE_DIR}/native/server/bindings.cpp"
    "${PROJECT_SOURCE_DIR}/native/server/packet_bindings.cpp"
    "${PROJECT_SOURCE_DIR}/native/server/binding_validation.cpp"
    "${PROJECT_SOURCE_DIR}/native/server/packet_io.cpp"
    "${PROJECT_SOURCE_DIR}/native/server/player_info.cpp"
    "${PROJECT_SOURCE_DIR}/native/server/identity.cpp"
    "${PROJECT_SOURCE_DIR}/native/server/teams.cpp")
target_include_directories(server_jni_test PRIVATE "${PROJECT_SOURCE_DIR}/native")
target_link_libraries(server_jni_test PRIVATE proxy_build_options)

# Existing JVM fixtures test mapped-name fallback; no Java is shipped in the DLL.
option(PROXY_JVM_TESTS "Build the existing JVM mapping tests" ON)
if(PROXY_JVM_TESTS)
    find_package(Java 17 REQUIRED COMPONENTS Development)
    add_library(mapped_method_test SHARED mapped_method_test.cpp)
    target_include_directories(mapped_method_test PRIVATE "${PROJECT_SOURCE_DIR}/native")
    set(fixture "${CMAKE_CURRENT_BINARY_DIR}/java/MappedMethodTest.class")
    add_custom_command(OUTPUT "${fixture}"
        COMMAND "${Java_JAVAC_EXECUTABLE}" --release 17
            -d "${CMAKE_CURRENT_BINARY_DIR}/java"
            "${CMAKE_CURRENT_SOURCE_DIR}/java/MappedMethodTest.java"
        DEPENDS java/MappedMethodTest.java VERBATIM)
    add_custom_target(mapped_method_test_classes ALL DEPENDS "${fixture}")
    add_test(NAME mapped_method COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni -Xverify:all
        -cp "${CMAKE_CURRENT_BINARY_DIR}/java" MappedMethodTest "$<TARGET_FILE:mapped_method_test>")
    set_tests_properties(mapped_method PROPERTIES TIMEOUT 30)
endif()
