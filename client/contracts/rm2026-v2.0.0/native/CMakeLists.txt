include("${PROJECT_SOURCE_DIR}/cmake/rm2026-dependencies.cmake")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/client.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/client.pb.h"
    COMMAND "${RM2026_PROTOC}" "--cpp_out=${CMAKE_CURRENT_BINARY_DIR}"
        "--proto_path=${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}/../client.proto"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../client.proto" VERBATIM)
add_library(rm2026_mqtt_reference
    "${PROJECT_SOURCE_DIR}/src/protocol/rm2026/mqtt_observer.cpp"
    "${PROJECT_SOURCE_DIR}/src/protocol/rm2026/state_mapping.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/client.pb.cc")
target_include_directories(rm2026_mqtt_reference PUBLIC "${PROJECT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(rm2026_mqtt_reference PUBLIC PAHO_MQTT_STATIC=1)
target_link_libraries(rm2026_mqtt_reference PUBLIC Qt6::Core
    eclipse-paho-mqtt-c::paho-mqtt3a-static PRIVATE protobuf::libprotobuf)
target_compile_options(rm2026_mqtt_reference PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra>)
add_executable(rm2026_mqtt_probe "${PROJECT_SOURCE_DIR}/tools/rm2026_mqtt_probe.cpp")
target_link_libraries(rm2026_mqtt_probe PRIVATE rm2026_mqtt_reference)
target_compile_options(rm2026_mqtt_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra>)
option(RM2026_BUILD_OBSERVER_VIEW "Separate loopback-only read-only developer window" OFF)
if(RM2026_BUILD_OBSERVER_VIEW)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Widgets)
    # Reuse the probe's CLI, validation and polling; never link the normal terminal.
    add_executable(rm2026_mqtt_viewer "${PROJECT_SOURCE_DIR}/tools/rm2026_mqtt_probe.cpp")
    target_compile_definitions(rm2026_mqtt_viewer PRIVATE RM2026_OBSERVER_VIEW=1)
    target_link_libraries(rm2026_mqtt_viewer PRIVATE rm2026_mqtt_reference Qt6::Widgets)
    target_compile_options(rm2026_mqtt_viewer PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra>)
    if(BUILD_TESTING)
        add_executable(rm2026_observer_view_tests "${PROJECT_SOURCE_DIR}/tests/rm2026_observer_view_tests.cpp")
        target_link_libraries(rm2026_observer_view_tests PRIVATE Qt6::Widgets)
        target_compile_options(rm2026_observer_view_tests PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-UNDEBUG>)
        add_test(NAME rm2026_observer_view_tests COMMAND rm2026_observer_view_tests)
        set_tests_properties(rm2026_observer_view_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
    endif()
endif()
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME rm2026_native_mqtt_tests COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/test_rm2026_native.py" --exe $<TARGET_FILE:rm2026_mqtt_probe>)
        set_tests_properties(rm2026_native_mqtt_tests PROPERTIES TIMEOUT 90)
        add_test(NAME rm2026_ingress_tests COMMAND "${Python3_EXECUTABLE}" -S
            "${PROJECT_SOURCE_DIR}/tests/test_rm2026_ingress.py" --exe $<TARGET_FILE:rm2026_mqtt_probe>)
        set_tests_properties(rm2026_ingress_tests PROPERTIES TIMEOUT 35)
        if(RM2026_BUILD_OBSERVER_VIEW)
            add_test(NAME rm2026_viewer_mqtt_tests COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/test_rm2026_native.py" --exe $<TARGET_FILE:rm2026_mqtt_viewer>)
            set_tests_properties(rm2026_viewer_mqtt_tests PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
            add_test(NAME rm2026_demo_tests COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/test_rm2026_demo.py" --exe $<TARGET_FILE:rm2026_mqtt_viewer>)
            set_tests_properties(rm2026_demo_tests PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
        endif()
    endif()
endif()
