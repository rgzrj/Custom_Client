# Keep downloaded source intact. Retain upstream notices in the build-only copy.
set(packetSource "${paho_SOURCE_DIR}/src/MQTTPacket.c")
file(SHA256 "${packetSource}" packetHash)
if(NOT packetHash STREQUAL "00ac469a6ae791ad17e15410d485cfeeb1a175a36148e49dbb4cd0ee753c7f2f")
    message(FATAL_ERROR "Historical ingress patch requires the exact pinned Paho 1.3.14 MQTTPacket.c")
endif()
file(READ "${packetSource}" packetCode)
string(REPLACE "#include \"Heap.h\"" "#include \"Heap.h\"\n#include \"protocol/rm2026/paho_ingress.h\"" packetCode "${packetCode}")
string(REPLACE "/* now read the rest, the variable header and payload */" [=[
/* Reject length/type before WebSocket_getdata can allocate the packet body. */
    if (!rm2026_header_allowed(header.byte, remaining_length))
    {
        *error = SOCKET_ERROR;
        goto exit;
    }
    /* now read the rest, the variable header and payload */]=] packetCode "${packetCode}")
string(REPLACE "ptype = header.bits.type;" [=[
if (!rm2026_body_allowed(header.byte, data, remaining_length) || !rm2026_receive_budget(remaining_length))
        {
            *error = SOCKET_ERROR;
            goto exit;
        }
        ptype = header.bits.type;]=] packetCode "${packetCode}")
set(patchedPacket "${CMAKE_CURRENT_BINARY_DIR}/MQTTPacket-bounded.c")
file(CONFIGURE OUTPUT "${patchedPacket}" CONTENT "${packetCode}" @ONLY)
get_target_property(packetSources common_obj_static SOURCES)
set(boundedSources "")
foreach(source IN LISTS packetSources)
    if(source STREQUAL "MQTTPacket.c")
        list(APPEND boundedSources "${patchedPacket}")
    else()
        list(APPEND boundedSources "${source}")
    endif()
endforeach()
set_property(TARGET common_obj_static PROPERTY SOURCES "${boundedSources}")
target_include_directories(common_obj_static PRIVATE "${paho_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src")
