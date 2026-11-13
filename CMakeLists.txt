cmake_minimum_required(VERSION 3.16)
project(option_risk_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SQLite3 REQUIRED)

add_library(pricing src/pricing.cpp)
target_include_directories(pricing PUBLIC include)
target_compile_options(pricing PRIVATE -Wall -Wextra -Wpedantic)

add_library(db src/db.cpp)
target_include_directories(db PUBLIC include)
target_link_libraries(db PUBLIC SQLite::SQLite3)
target_compile_options(db PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ore_run src/main.cpp)
target_link_libraries(ore_run PRIVATE pricing db)
target_compile_options(ore_run PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(test_pricing tests/test_pricing.cpp)
target_link_libraries(test_pricing PRIVATE pricing)
add_test(NAME pricing_tests COMMAND test_pricing)
