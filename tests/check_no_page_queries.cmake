# Fails when a page query is called anywhere outside the startup-only files.
# A per-access VirtualQuery in a presentation or capture path costs
# milliseconds per frame; those paths use guarded_memory.h instead.
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(allowed
    bootstrap.cpp
    host_validation.cpp
    target_host.cpp
    target_mapping.cpp
)

file(GLOB sources "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.inc")
set(offenders "")
foreach(source IN LISTS sources)
    get_filename_component(name "${source}" NAME)
    if(name IN_LIST allowed)
        continue()
    endif()
    file(READ "${source}" contents)
    # A call, not a mention in a comment.
    string(REGEX MATCH "VirtualQuery(Ex)?[ \t\r\n]*\\(" hit "${contents}")
    if(hit)
        list(APPEND offenders "${name}")
    endif()
endforeach()

if(offenders)
    message(FATAL_ERROR "Page query outside startup files: ${offenders}")
endif()
message(STATUS "no_page_queries: PASS")
