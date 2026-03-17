cmake_minimum_required(VERSION 3.10)
project(LonelyRunnersTests CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the main executable
add_executable(lonely_runners main.cpp)
target_compile_options(lonely_runners PRIVATE -O3 -Wall -Wextra)

# Create test executable
add_executable(test_main test_main.cpp)
target_compile_options(test_main PRIVATE -O2 -Wall -Wextra)

# Create happy test executable
add_executable(happy happy.cpp)
target_compile_options(happy PRIVATE -O3 -Wall -Wextra)

# Enable testing
enable_testing()
add_test(NAME MainTests  COMMAND test_main)
add_test(NAME HappyTests COMMAND happy)

# Add a custom target to run tests
add_custom_target(run_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --verbose
  DEPENDS test_main happy
)

# Print configuration info
message(STATUS "Build configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Main executable: lonely_runners")
message(STATUS "  Test executable: test_main")
message(STATUS "  Happy executable: happy")
message(STATUS "")
message(STATUS "To build:")
message(STATUS "  cmake -B build")
message(STATUS "  cmake --build build")
message(STATUS "")
message(STATUS "To run tests:")
message(STATUS "  cmake --build build --target run_tests")
message(STATUS "  or: ./build/test_main")
message(STATUS "  or: ./build/happy")
message(STATUS "")
message(STATUS "To run main program:")
message(STATUS "  ./build/lonely_runners")
