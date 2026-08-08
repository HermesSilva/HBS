cmake_minimum_required(VERSION 3.16)
project(gaitnet_mcp CXX)

# Pure C++17 model-editing MCP over the .mass project (no DART, no Python).
# Copied/adapted from MASS-Easy's libmassedit and re-targeted to this project's
# .mass schema (adds the GaitNet env.xml config + <parameter> block + joint kp/kv).
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(boost_asio CONFIG REQUIRED)

set(_lib_srcs
    MassModel.cpp Index.cpp Kinematics.cpp DofMap.cpp Batch.cpp
    Complete.cpp Query.cpp Atlas.cpp Groom.cpp BindSkin.cpp Skeleton.cpp Muscles.cpp Mcp.cpp)

# The editing core, shared by the standalone server and the Arena editor (which
# hosts the same tools in-process so an agent can drive the open model).
add_library(massedit STATIC ${_lib_srcs})
target_include_directories(massedit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(massedit PUBLIC cxx_std_17)
target_compile_definitions(massedit PUBLIC _WIN32_WINNT=0x0601 BOOST_ASIO_NO_DEPRECATED)
target_link_libraries(massedit PUBLIC
    nlohmann_json::nlohmann_json
    tinyxml2::tinyxml2
    Boost::asio)
if(WIN32)
  target_link_libraries(massedit PUBLIC ws2_32 mswsock)
endif()

# Standalone MCP server over TCP (newline-delimited JSON-RPC).
add_executable(gaitnet-mcp server/main.cpp)
target_compile_features(gaitnet-mcp PRIVATE cxx_std_17)
target_link_libraries(gaitnet-mcp PRIVATE massedit)
