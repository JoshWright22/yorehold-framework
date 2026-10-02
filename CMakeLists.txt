cmake_minimum_required(VERSION 3.24)
project(YoreholdFramework LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(SDL3)

add_library(yorehold-framework
    src/Host.cpp
)
add_library(yorehold::framework ALIAS yorehold-framework)

target_include_directories(yorehold-framework PUBLIC include)
target_link_libraries(yorehold-framework PUBLIC SDL3::SDL3)

if(MSVC)
    target_compile_options(yorehold-framework PRIVATE /W4)
else()
    target_compile_options(yorehold-framework PRIVATE -Wall -Wextra)
endif()
