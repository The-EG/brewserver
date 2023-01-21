cmake_minimum_required(VERSION 3.16)

project(brewserver CXX C)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)

find_package(Freetype REQUIRED)

add_subdirectory(contrib/spdlog)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(contrib/nlohmann_json)

set(CIVETWEB_OPTIONS
    USE_WEBSOCKET
    NO_SSL
    MG_EXPERIMENTAL_INTERFACES
    NO_FILES
)

add_executable(brewserver
    src/main.cpp
    src/app.cpp
    src/app.h
    src/st7920.cpp
    src/st7920.h

    src/relay.cpp
    src/relay.h
    src/temp_sensor.cpp
    src/temp_sensor.h

    contrib/civetweb/src/civetweb.c

)
target_compile_definitions(brewserver PRIVATE ${CIVETWEB_OPTIONS})
target_include_directories(brewserver PRIVATE contrib/civetweb/include)
target_link_libraries(brewserver PRIVATE Freetype::Freetype pthread spdlog::spdlog nlohmann_json::nlohmann_json)
