cmake_minimum_required(VERSION 3.16)

# Loads a training set's env.xml into the real engine and steps it. The gate a
# new muscle structure passes before anything is trained on it.
add_executable(envcheck main.cpp)
target_compile_features(envcheck PRIVATE cxx_std_17)
target_link_libraries(envcheck PRIVATE sim)
set_target_properties(envcheck PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)
