project(sniffcraft)

# Everything related to the Beta 1.7.3 protocol: the packets definition (include/protocol),
# the glue around it and the json implementation used to display packets
set(sniffcraft_protocol_HDR
    include/protocol/base_structs.h
    include/protocol/base_types.h
    include/protocol/constants.h
    include/protocol/dimensions.h
    include/protocol/inventory/item_stack.h
    include/protocol/network_stream.h
    include/protocol/numeric_structs.h
    include/protocol/packet_data.h
    include/protocol/packet_ids.h
    include/protocol/packets.h
    include/protocol/quantized_types.h

    include/sniffcraft/Protocol.hpp
    include/sniffcraft/Json/Json.hpp
    include/sniffcraft/Json/RecursiveWrapper.hpp
)

set(sniffcraft_protocol_SRC
    src/Protocol.cpp
    src/Json/Json.cpp
)

add_library(sniffcraft_protocol STATIC ${sniffcraft_protocol_SRC} ${sniffcraft_protocol_HDR})
set_property(TARGET sniffcraft_protocol PROPERTY CXX_STANDARD 17)
target_include_directories(sniffcraft_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# packets.h includes some of its own headers with <> (e.g. <packet_ids.h>)
target_include_directories(sniffcraft_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/protocol")
if(MSVC)
    set_target_properties(sniffcraft_protocol PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif(MSVC)

set(sniffcraft_PUBLIC_HDR
    include/sniffcraft/BaseProxy.hpp
    include/sniffcraft/conf.hpp
    include/sniffcraft/Connection.hpp
    include/sniffcraft/DataProcessor.hpp
    include/sniffcraft/enums.hpp
    include/sniffcraft/Logger.hpp
    include/sniffcraft/LogItem.hpp
    include/sniffcraft/MinecraftProxy.hpp
    include/sniffcraft/NetworkRecapItem.hpp
    include/sniffcraft/server.hpp
)

set(sniffcraft_SRC
    src/BaseProxy.cpp
    src/conf.cpp
    src/Connection.cpp
    src/Logger.cpp
    src/MinecraftProxy.cpp
    src/server.cpp
    src/main.cpp
)

# To have a nice files structure in Visual Studio
if(MSVC)
    foreach(source IN LISTS sniffcraft_PUBLIC_HDR)
        get_filename_component(source_path_header "${source}" PATH)
        string(REPLACE "include/sniffcraft" "Header Files/public" source_path_header "${source_path_header}")
        string(REPLACE "/" "\\" source_path_msvc "${source_path_header}")
        source_group("${source_path_msvc}" FILES "${source}")
    endforeach()

    foreach(source IN LISTS sniffcraft_SRC)
        get_filename_component(source_path "${source}" PATH)
        string(REPLACE "src" "Source Files" source_path "${source_path}")
        string(REPLACE "/" "\\" source_path_msvc "${source_path}")
        source_group("${source_path_msvc}" FILES "${source}")
    endforeach()
endif()

add_executable(${PROJECT_NAME} ${sniffcraft_SRC} ${sniffcraft_PUBLIC_HDR})
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "_d")
set_target_properties(${PROJECT_NAME} PROPERTIES RELWITHDEBINFO_POSTFIX "_rd")

if(MSVC)
    # To avoid having folder for each configuration when building with Visual
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/lib")
    set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/lib")
    set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/lib")
    set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/lib")

    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
    set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
endif(MSVC)

target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if (MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif(MSVC)

# Add Asio
target_link_libraries(${PROJECT_NAME} PUBLIC asio)
target_compile_definitions(${PROJECT_NAME} PUBLIC ASIO_STANDALONE)

# Add threads support
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)

# Add the Beta 1.7.3 protocol
target_link_libraries(${PROJECT_NAME} PUBLIC sniffcraft_protocol)

if (SNIFFCRAFT_WITH_GUI)
	target_link_libraries(${PROJECT_NAME} PRIVATE glfw ${OPENGL_LIBRARIES} imgui)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_GUI)
endif()
