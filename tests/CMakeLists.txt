find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

include(CTest)

add_executable(protocol_tests protocol_tests.cpp)
set_property(TARGET protocol_tests PROPERTY CXX_STANDARD 17)
target_link_libraries(protocol_tests PRIVATE sniffcraft_protocol Catch2::Catch2WithMain)
add_test(NAME protocol_tests COMMAND protocol_tests)

# Needs the real sniffcraft proxy, runs it against a fake client and server
add_executable(proxy_tests proxy_tests.cpp)
set_property(TARGET proxy_tests PROPERTY CXX_STANDARD 17)
add_dependencies(proxy_tests sniffcraft)
target_link_libraries(proxy_tests PRIVATE sniffcraft_protocol asio Threads::Threads Catch2::Catch2WithMain)
target_compile_definitions(proxy_tests PRIVATE ASIO_STANDALONE SNIFFCRAFT_EXE="$<TARGET_FILE:sniffcraft>")
add_test(NAME proxy_tests COMMAND proxy_tests)
