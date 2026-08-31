cmake_minimum_required(VERSION 3.24)
project(Horelac VERSION 1.2.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

option(HORELAC_BUILD_BOT "Build the Discord bot adapter" ON)
option(HORELAC_BUILD_RENDERER "Build the Cairo renderer" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(horelac_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

find_package(SQLite3 REQUIRED)

add_library(horelac_core
    src/domain/ScheduleAlgorithms.cpp
    src/services/ScheduleService.cpp
    src/persistence/sqlite/SQLiteScheduleRepository.cpp
    src/support/Config.cpp
    src/support/Localization.cpp)
target_include_directories(horelac_core PUBLIC include)
target_link_libraries(horelac_core PUBLIC SQLite::SQLite3)
horelac_warnings(horelac_core)

if(HORELAC_BUILD_RENDERER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
    add_library(horelac_renderer src/render/cairo/CairoScheduleRenderer.cpp)
    target_include_directories(horelac_renderer PUBLIC include)
    target_link_libraries(horelac_renderer PUBLIC horelac_core PkgConfig::CAIRO)
    horelac_warnings(horelac_renderer)
endif()

if(HORELAC_BUILD_BOT)
    find_package(dpp CONFIG QUIET)
    if(NOT dpp_FOUND)
        FetchContent_Declare(dpp
            GIT_REPOSITORY https://github.com/brainboxdotcc/DPP.git
            GIT_TAG v10.1.3
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(dpp)
    endif()
    if(TARGET dpp AND NOT TARGET dpp::dpp)
        add_library(dpp::dpp ALIAS dpp)
    endif()

    add_executable(horelac src/main.cpp src/bot/DiscordBot.cpp src/services/RenderQueue.cpp)
    target_include_directories(horelac PRIVATE include)
    target_link_libraries(horelac PRIVATE horelac_core dpp::dpp)
    if(HORELAC_BUILD_RENDERER)
        target_link_libraries(horelac PRIVATE horelac_renderer)
        target_compile_definitions(horelac PRIVATE HORELAC_HAS_CAIRO=1)
    endif()
    horelac_warnings(horelac)
endif()

if(BUILD_TESTING)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    add_executable(horelac_tests
        tests/unit/ScheduleAlgorithmsTests.cpp
        tests/unit/ScheduleServiceTests.cpp
        tests/integration/SQLiteRepositoryTests.cpp)
    target_link_libraries(horelac_tests PRIVATE horelac_core Catch2::Catch2WithMain)
    if(HORELAC_BUILD_RENDERER)
        target_sources(horelac_tests PRIVATE tests/unit/ScheduleRendererTests.cpp)
        target_link_libraries(horelac_tests PRIVATE horelac_renderer)
    endif()
    horelac_warnings(horelac_tests)
    catch_discover_tests(horelac_tests)
endif()

if(TARGET horelac)
    install(TARGETS horelac RUNTIME DESTINATION bin)
endif()
install(DIRECTORY locales migrations DESTINATION share/horelac)
