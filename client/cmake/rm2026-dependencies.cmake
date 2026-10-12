# Shared pinned source dependencies for the imported backend and historical tools.
include_guard(GLOBAL)
enable_language(C)
find_package(Qt6 6.9 REQUIRED COMPONENTS Core)
include(FetchContent)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBUPB OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(PAHO_WITH_SSL OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_CPACK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(abseil
    URL https://codeload.github.com/abseil/abseil-cpp/zip/refs/tags/20240116.2
    URL_HASH SHA256=69909dd729932cbbabb9eeaff56179e8d124515f5d3ac906663d573d700b4c7d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(protobuf
    URL https://codeload.github.com/protocolbuffers/protobuf/zip/refs/tags/v29.3
    URL_HASH SHA256=85803e01f347141e16a2f770213a496f808fff9f0138c7c0e0c9dfa708b0da92
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(paho
    URL https://codeload.github.com/eclipse-paho/paho.mqtt.c/zip/refs/tags/v1.3.14
    URL_HASH SHA256=ad67f3920b4dc618867c573626f6dbddc213d3f759abbdb9d785f7f85d086e41
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(abseil protobuf paho)
set(rm2026LicenseDir "${PROJECT_BINARY_DIR}/upstream-licenses")
file(MAKE_DIRECTORY "${rm2026LicenseDir}")
configure_file("${abseil_SOURCE_DIR}/LICENSE" "${rm2026LicenseDir}/Abseil-LICENSE" COPYONLY)
configure_file("${protobuf_SOURCE_DIR}/LICENSE" "${rm2026LicenseDir}/Protobuf-LICENSE" COPYONLY)
configure_file("${protobuf_SOURCE_DIR}/third_party/utf8_range/LICENSE" "${rm2026LicenseDir}/utf8_range-LICENSE" COPYONLY)
foreach(pahoNotice LICENSE NOTICE edl-v10 epl-v20)
    configure_file("${paho_SOURCE_DIR}/${pahoNotice}" "${rm2026LicenseDir}/Paho-${pahoNotice}" COPYONLY)
endforeach()
include("${PROJECT_SOURCE_DIR}/contracts/rm2026-v2.0.0/native/paho-ingress.cmake")

set(RM2026_PROTOC "" CACHE FILEPATH "Developer protoc executable, exactly 29.3")
if(NOT RM2026_PROTOC)
    find_program(rm2026Protoc NAMES protoc HINTS "D:/YOLOV8/Library/bin")
    if(rm2026Protoc)
        set(RM2026_PROTOC "${rm2026Protoc}" CACHE FILEPATH "Developer protoc executable, exactly 29.3" FORCE)
    endif()
endif()
if(NOT EXISTS "${RM2026_PROTOC}")
    message(FATAL_ERROR "Set RM2026_PROTOC to a protoc 29.3 executable; do not link MSVC libraries.")
endif()
execute_process(COMMAND "${RM2026_PROTOC}" --version OUTPUT_VARIABLE protocVersion
    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE protocResult)
if(NOT protocResult EQUAL 0 OR NOT protocVersion STREQUAL "libprotoc 29.3")
    message(FATAL_ERROR "protoc must match the pinned 29.3 C++ runtime: ${protocVersion}")
endif()
