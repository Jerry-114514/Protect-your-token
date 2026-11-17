cmake_minimum_required(VERSION 3.20)

project(cmake_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(game_backend STATIC Manipulate.cpp)
target_include_directories(game_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(game_backend PUBLIC cxx_std_17)

add_executable(cmake_test main.cpp)
target_link_libraries(cmake_test PRIVATE game_backend)

include(CTest)
if(BUILD_TESTING)
    add_executable(game_backend_tests tests/GameWorldTests.cpp)
    target_link_libraries(game_backend_tests PRIVATE game_backend)
    add_test(NAME game_backend_tests COMMAND game_backend_tests)
endif()
