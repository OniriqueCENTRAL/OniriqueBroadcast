cmake_minimum_required(VERSION 3.22)

project(OniriqueBroadcast VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE : réutilise la copie déjà téléchargée par le projet VocalBox voisin si elle existe, sinon téléchargement
set(_LOCAL_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/../OniriqueVocalBox/build/_deps/juce-src")
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_JUCE AND EXISTS "${_LOCAL_JUCE}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${_LOCAL_JUCE}" CACHE PATH "JUCE source")
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(OniriqueBroadcast
    PRODUCT_NAME    "ONIRIQUE BROADCAST"
    COMPANY_NAME    "Onirique"
    VERSION         1.0.0
    ICON_BIG        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/logo.png"
    ICON_SMALL      "${CMAKE_CURRENT_SOURCE_DIR}/Resources/logo.png")


target_sources(OniriqueBroadcast PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Engine.cpp
    Source/WindowsPlatform.cpp
    Source/Audio/AudioRouter.cpp
    Source/Audio/CableInstaller.cpp
    Source/UI/Look.cpp
    Source/UI/Widgets.cpp
    Source/UI/Displays.cpp)

juce_add_binary_data(OniriqueBroadcastAssets SOURCES Resources/logo.png)

target_include_directories(OniriqueBroadcast PRIVATE Source)

target_compile_definitions(OniriqueBroadcast PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_ASIO=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:OniriqueBroadcast,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:OniriqueBroadcast,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(OniriqueBroadcast
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        OniriqueBroadcastAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- tests hors ligne du cœur audio (pont entre horloges, chaîne complète)
juce_add_console_app(OniriqueBroadcastTests PRODUCT_NAME "OniriqueBroadcastTests")
target_sources(OniriqueBroadcastTests PRIVATE Tests/Tests.cpp Source/Engine.cpp Source/WindowsPlatform.cpp)
target_include_directories(OniriqueBroadcastTests PRIVATE Source)
target_compile_definitions(OniriqueBroadcastTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(OniriqueBroadcastTests
    PRIVATE juce::juce_audio_processors juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
