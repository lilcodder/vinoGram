cmake_minimum_required(VERSION 3.25)
project(subscription_checks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(subscription_checks
    config_test.cpp
    ../../../Telegram/SourceFiles/core/proxy_subscription_config.cpp)
target_include_directories(subscription_checks PRIVATE
    ../../../Telegram/SourceFiles
    ../../../Telegram/lib_base
    ../../../Telegram/ThirdParty/GSL/include)
target_link_libraries(subscription_checks PRIVATE Qt6::Core)
enable_testing()
add_test(NAME subscription_parser COMMAND subscription_checks)
