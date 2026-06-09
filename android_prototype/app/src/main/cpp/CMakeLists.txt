cmake_minimum_required(VERSION 3.10)
project(audihaptic_native)

set(AUDIO_PROCESSOR_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../../)
add_library(audihaptic_native SHARED
    haptic-bridge.cpp
    ${AUDIO_PROCESSOR_ROOT}/AudioProcessor.cpp
)

set_target_properties(audihaptic_native PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

include_directories(${CMAKE_SOURCE_DIR} ${AUDIO_PROCESSOR_ROOT})
find_library(log-lib log)
if (log-lib)
    target_link_libraries(audihaptic_native PRIVATE ${log-lib})
endif()

target_link_libraries(audihaptic_native PRIVATE android)