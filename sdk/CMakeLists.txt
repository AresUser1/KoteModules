cmake_minimum_required(VERSION 3.20)
project(KotogramModules LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(include)

# Собираем все C/C++ файлы в src/ как shared library (.so)
file(GLOB SRC_FILES "src/*.c" "src/*.cpp")

foreach(SRC ${SRC_FILES})
    get_filename_component(MOD_NAME ${SRC} NAME_WE)
    add_library(${MOD_NAME} SHARED ${SRC})
    set_target_properties(${MOD_NAME} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out"
    )
endforeach()
