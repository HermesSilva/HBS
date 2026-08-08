cmake_minimum_required(VERSION 3.16)
project(gaitnet)

set(OpenGL_GL_PREFERENCE LEGACY)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif(NOT CMAKE_BUILD_TYPE)

if(NOT MSVC)
  link_libraries(stdc++fs)
  add_compile_options(-fPIC)
  add_compile_options(-Wdeprecated-declarations)
  add_compile_options(-DHAVE_CSTDDEF)
else()
  # MSVC: disable min/max macros, enable M_PI and lax preprocessor for DART headers
  add_compile_definitions(NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
  add_compile_options(/bigobj /permissive-)
endif()

add_definitions(-DMASS_ROOT_DIR="${CMAKE_HOME_DIRECTORY}")
set(CMAKE_MODULE_PATH "${CMAKE_HOME_DIRECTORY}/cmake")

# Every executable lands in Dist/x64/<Config>, next to the Arena.vcxproj output
# that already used that layout, so there is one place to run things from and
# vcpkg's applocal deploy puts each DLL beside the exes that need it. The
# per-config variables are what a multi-config generator honours; setting the
# plain one would make it append a second <Config> level.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_SOURCE_DIR}/Dist/x64/Release")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_SOURCE_DIR}/Dist/x64/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/Dist/x64/RelWithDebInfo")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_SOURCE_DIR}/Dist/x64/MinSizeRel")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_SOURCE_DIR}/Dist/x64")

# Build the OpenGL/ImGui viewer (needs a display). Off for headless/server builds.
option(GAITNET_BUILD_VIEWER "Build the GLFW/ImGui viewer" ON)
if(SERVER_BUILD)
  set(GAITNET_BUILD_VIEWER OFF)
endif()

add_subdirectory( sim )
if(GAITNET_BUILD_VIEWER)
  add_subdirectory( libs )
  add_subdirectory( viewer )
endif()

# envcheck: validates a training set against the real engine (needs sim only).
add_subdirectory( tools/envcheck )

# Model-editing core + MCP server (pure C++17 over the .mass project; no
# DART/Python). Comes before the editor, which links the `massedit` library to
# host the same tools in-process.
option(GAITNET_BUILD_MCP "Build the .mass MCP server" ON)
if(GAITNET_BUILD_MCP)
  add_subdirectory( mcp )
endif()

# Arena-style visual character editor (GaitNet formats). Needs a display.
option(GAITNET_BUILD_EDITOR "Build the visual character editor" ON)
if(SERVER_BUILD)
  set(GAITNET_BUILD_EDITOR OFF)
endif()
if(GAITNET_BUILD_EDITOR)
  add_subdirectory( editor )
endif()
