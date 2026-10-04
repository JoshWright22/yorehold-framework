cmake_minimum_required(VERSION 3.24)
project(YoreholdFramework LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The prebuilt Dawn package uses the release C runtime, so every config does too (Debug still has no optimisation).
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(CTest)
option(YOREHOLD_FRAMEWORK_UNIT_TESTS "Build framework regression tests" ${BUILD_TESTING})

option(YOREHOLD_FRAMEWORK_TESTS "Build the framework's visual test browser" ON)
# F12 screenshots and notes from dev builds land here, outside every repo.
set(YOREHOLD_FEEDBACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../feedback" CACHE PATH "Where F12 feedback is saved")
set(YOREHOLD_DEV_STATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.dev" CACHE PATH "Where dev builds remember window position and test scene")

include(FetchContent)

if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(SDL3)

# JSON for save files, rulesets and chapters.
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(json)

# Zip archives (.yoreskin skins, .yore chapters).
FetchContent_Declare(
    miniz
    URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
)
FetchContent_MakeAvailable(miniz)
add_library(yorehold-miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(yorehold-miniz PUBLIC ${miniz_SOURCE_DIR})

# Font rasteriser and image decoders (single headers, public domain). stb_image_write is only
# used by the regression tests, to make JPEG/PNG/BMP/TGA files to decode.
set(stb_dir "${CMAKE_BINARY_DIR}/_deps/stb")
foreach(stb_file
        "stb_truetype.h=ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab"
        "stb_image.h=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3"
        "stb_image_write.h=cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05")
    string(REPLACE "=" ";" stb_parts "${stb_file}")
    list(GET stb_parts 0 stb_name)
    list(GET stb_parts 1 stb_hash)
    if(NOT EXISTS "${stb_dir}/${stb_name}")
        file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/${stb_name}
            "${stb_dir}/${stb_name}"
            EXPECTED_HASH SHA256=${stb_hash})
    endif()
endforeach()

# WebP decoding (many map and token packs ship as .webp). Only the libraries, none of the tools.
foreach(webp_option ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO WEBPMUX EXTRAS)
    set(WEBP_BUILD_${webp_option} OFF CACHE BOOL "" FORCE)
endforeach()
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libwebp
    URL https://github.com/webmproject/libwebp/archive/refs/tags/v1.6.0.tar.gz
    URL_HASH SHA256=93a852c2b3efafee3723efd4636de855b46f9fe1efddd607e1f42f60fc8f2136
)
FetchContent_MakeAvailable(libwebp)

include(cmake/WebGpu.cmake)

# Dawn's D3D12 backend loads Microsoft's shader compiler DLLs at runtime; ship them next to every exe.
if(WIN32)
    find_path(YOREHOLD_D3D_REDIST dxcompiler.dll
        PATHS "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Redist/D3D/x64" REQUIRED NO_DEFAULT_PATH)
endif()
function(yorehold_copy_gpu_runtime target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${YOREHOLD_D3D_REDIST}/dxcompiler.dll" "${YOREHOLD_D3D_REDIST}/dxil.dll" "${YOREHOLD_D3D_REDIST}/d3dcompiler_47.dll"
                "$<TARGET_FILE_DIR:${target}>")
    endif()
endfunction()

function(yorehold_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(yorehold-framework
    src/Host.cpp
    src/Scenes.cpp
    src/animation/Cutscene.cpp
    src/animation/SpriteSheet.cpp
    src/animation/Tween.cpp
    src/audio/Audio.cpp
    src/assets/Assets.cpp
    src/assets/Skin.cpp
    src/assets/FileSystem.cpp
    src/debug/Profiler.cpp
    src/editor/History.cpp
    src/graphics/Atlas.cpp
    src/graphics/Font.cpp
    src/graphics/Camera.cpp
    src/graphics/DebugFont.cpp
    src/graphics/Image.cpp
    src/graphics/Renderer.cpp
    src/graphics/Particles.cpp
    src/graphics/Lighting.cpp
    src/input/ControlScheme.cpp
    src/input/Input.cpp
    src/input/InputMapJson.cpp
    src/map/CameraControls.cpp
    src/map/Grid.cpp
    src/map/Positioning.cpp
    src/map/FogOfWar.cpp
    src/map/LightLevels.cpp
    src/map/Objects.cpp
    src/map/Regions.cpp
    src/map/Navigation.cpp
    src/map/Pathfinding.cpp
    src/map/Templates.cpp
    src/map/TileMap.cpp
    src/map/Tokens.cpp
    src/net/Http.cpp
    src/net/Loopback.cpp
    src/net/Session.cpp
    src/net/Tcp.cpp
    src/rpg/Action.cpp
    src/rpg/Reaction.cpp
    src/rpg/Character.cpp
    src/rpg/CharacterJson.cpp
    src/rpg/Combat.cpp
    src/rpg/Conditions.cpp
    src/rpg/Dice.cpp
    src/rpg/Compendium.cpp
    src/rpg/Dialogue.cpp
    src/rpg/Effect.cpp
    src/rpg/EffectJson.cpp
    src/rpg/QuestJournal.cpp
    src/rpg/Ruleset.cpp
    src/rpg/RulesetJson.cpp
    src/rpg/PositioningRules.cpp
    src/rpg/Stats.cpp
    src/rpg/Stealth.cpp
    src/rpg/Tactics.cpp
    src/save/SaveFile.cpp
    src/text/RichText.cpp
    src/text/Strings.cpp
    src/ui/TextEdit.cpp
    src/ui/Ui.cpp
    src/testing/TestBrowser.cpp
)
add_library(yorehold::framework ALIAS yorehold-framework)

target_include_directories(yorehold-framework
    PUBLIC include
    PRIVATE src ${stb_dir}
)
target_link_libraries(yorehold-framework
    # Public because save migrations edit documents as nlohmann::json.
    PUBLIC SDL3::SDL3 nlohmann_json::nlohmann_json
    PRIVATE ${YOREHOLD_WEBGPU_TARGET} yorehold-miniz webp
)
if(WIN32)
    target_link_libraries(yorehold-framework PRIVATE ws2_32 winhttp)
endif()

if(EMSCRIPTEN)
    set(framework_assets_path "/framework-assets")
else()
    set(framework_assets_path "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
target_compile_definitions(yorehold-framework INTERFACE
    YH_FEEDBACK_DIR="${YOREHOLD_FEEDBACK_DIR}"
    YH_DEV_STATE_DIR="${YOREHOLD_DEV_STATE_DIR}"
    YH_FRAMEWORK_ASSETS="${framework_assets_path}"
)
yorehold_warnings(yorehold-framework)

if(YOREHOLD_FRAMEWORK_TESTS)
    if(ANDROID)
        add_library(yorehold-framework-tests SHARED tests/visual/main.cpp)
    else()
        add_executable(yorehold-framework-tests tests/visual/main.cpp)
    endif()
    target_link_libraries(yorehold-framework-tests PRIVATE yorehold::framework)
    yorehold_warnings(yorehold-framework-tests)
    yorehold_copy_gpu_runtime(yorehold-framework-tests)
    yorehold_platform_app(yorehold-framework-tests)
endif()

if(YOREHOLD_FRAMEWORK_UNIT_TESTS AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(yorehold-framework-unit-tests
        tests/unit/main.cpp
        tests/unit/ImageTests.cpp
        tests/unit/DialogueTests.cpp
        tests/unit/QuestTests.cpp
        tests/unit/ConditionTests.cpp
        tests/unit/EffectTests.cpp
        tests/unit/CombatTests.cpp
        tests/unit/PositioningTests.cpp
        tests/unit/ProficiencyTests.cpp
        tests/unit/CameraControlTests.cpp
        tests/unit/CutsceneTests.cpp
    )
    target_link_libraries(yorehold-framework-unit-tests PRIVATE yorehold::framework webp)
    target_include_directories(yorehold-framework-unit-tests PRIVATE ${stb_dir})
    yorehold_warnings(yorehold-framework-unit-tests)
    yorehold_copy_gpu_runtime(yorehold-framework-unit-tests)
    add_test(NAME framework-unit COMMAND yorehold-framework-unit-tests)
endif()
