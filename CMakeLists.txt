# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(box3-assistant)

add_custom_target(unit-tests
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_unit_tests.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
