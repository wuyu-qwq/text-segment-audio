cmake_minimum_required(VERSION 3.18)
project(text_segment_audio C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PROJECT_SOURCES
    src/main.c
    src/common.c
    src/app_config.c
    src/fs_utils.c
    src/segment_builder.c
    src/sapi_tts.c
    src/worker_pool.c
)

add_executable(text_segment_audio ${PROJECT_SOURCES})
target_include_directories(text_segment_audio PRIVATE include)
target_compile_definitions(text_segment_audio PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(text_segment_audio PRIVATE /W4)
else()
    target_compile_options(text_segment_audio PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2)
endif()

target_link_libraries(text_segment_audio PRIVATE ole32 uuid sapi)
