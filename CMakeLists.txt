cmake_minimum_required(VERSION 3.15)

project(SniffCraft)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(SNIFFCRAFT_WITH_GUI "Activate for GUI support" ON)
option(SNIFFCRAFT_BUILD_TESTS "Build the unit and integration tests" OFF)

include(FetchContent)

# Add Asio
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/asio.cmake")

# Add GUI related dependencies
if(SNIFFCRAFT_WITH_GUI)
    set(FETCHCONTENT_QUIET TRUE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/opengl.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/glfw.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/imgui.cmake")
endif(SNIFFCRAFT_WITH_GUI)

# Check pthreads
find_package(Threads)

add_subdirectory(sniffcraft)

if(SNIFFCRAFT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif(SNIFFCRAFT_BUILD_TESTS)
