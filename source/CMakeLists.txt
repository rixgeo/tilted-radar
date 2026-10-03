cmake_minimum_required(VERSION 3.15)
project(TiltedRadar CXX)

# ------------------------------------------------------------------------------
# Language / toolchain
# ------------------------------------------------------------------------------
# Plugin-SDK's injector uses C++20 concepts; this SDK also uses std::expected.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Plugin-SDK
# ------------------------------------------------------------------------------
# Path to your unpacked and built plugin-sdk folder.
# Override with: cmake -DPLUGIN_SDK_DIR=D:/path/to/plugin-sdk ...
set(PLUGIN_SDK_DIR "C:/plugin-sdk" CACHE PATH "Path to the plugin-sdk folder")

# Include the game headers so the IDE understands game classes like CPed, CVehicle, etc.
include_directories(
    "${PLUGIN_SDK_DIR}/shared"
    "${PLUGIN_SDK_DIR}/shared/game"
    "${PLUGIN_SDK_DIR}/shared/dxsdk"
    "${PLUGIN_SDK_DIR}/stb"
    "${PLUGIN_SDK_DIR}/injector"
    "${PLUGIN_SDK_DIR}/plugin_sa"
    "${PLUGIN_SDK_DIR}/plugin_sa/game_sa"
    "${PLUGIN_SDK_DIR}/plugin_sa/game_sa/enums"
    "${PLUGIN_SDK_DIR}/plugin_sa/game_sa/rw"
    "${PLUGIN_SDK_DIR}/safetyhook"
)

# ------------------------------------------------------------------------------
# Vendored The-Definitive-UI radar sources
# ------------------------------------------------------------------------------
set(TDU_RADAR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/the-definitive-ui/source/tdui")
include_directories(
    "${TDU_RADAR_ROOT}"
    "${TDU_RADAR_ROOT}/radar"
    "${TDU_RADAR_ROOT}/radar/camera"
    "${TDU_RADAR_ROOT}/radar/map"
    "${TDU_RADAR_ROOT}/radar/map/gangzone"
    "${TDU_RADAR_ROOT}/radar/render"
    "${TDU_RADAR_ROOT}/radar/render/draw"
    "${TDU_RADAR_ROOT}/core"
    "${TDU_RADAR_ROOT}/config"
    "${TDU_RADAR_ROOT}/game"
    "${TDU_RADAR_ROOT}/util"
    "${TDU_RADAR_ROOT}/gfx"
    "${TDU_RADAR_ROOT}/gfx/shaders"
    "${TDU_RADAR_ROOT}/localization"
)

add_compile_definitions(GTASA PLUGIN_SGV_10US RW)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------
file(GLOB_RECURSE RADAR_SOURCES CONFIGURE_DEPENDS
    "${TDU_RADAR_ROOT}/radar/*.cpp"
)

set(SOURCES
    "src/main.cpp"
    "src/RadarMod.cpp"
    "${TDU_RADAR_ROOT}/config/Config.cpp"
    "${TDU_RADAR_ROOT}/game/GameState.cpp"
    "${TDU_RADAR_ROOT}/core/ModPaths.cpp"
    "${TDU_RADAR_ROOT}/util/MathUtils.cpp"
    "${TDU_RADAR_ROOT}/gfx/Shader.cpp"
    "${TDU_RADAR_ROOT}/gfx/ShaderManager.cpp"
    "${TDU_RADAR_ROOT}/gfx/TxdManager.cpp"
    "${TDU_RADAR_ROOT}/gfx/Base64Image.cpp"
    "${TDU_RADAR_ROOT}/gfx/Draw.cpp"
    ${RADAR_SOURCES}
)

# ------------------------------------------------------------------------------
# Plugin-SDK import library
# ------------------------------------------------------------------------------
# Plugin-SDK's Visual Studio build exports the import library as Plugin_d.lib.
# The SDK is built against the DLL CRT, so keep the plugin on the matching runtime
# to avoid the linker mismatch that breaks ASI builds.
add_library(plugin_sa STATIC IMPORTED)
set_target_properties(plugin_sa PROPERTIES
    IMPORTED_LOCATION "${PLUGIN_SDK_DIR}/output/lib/Plugin_d.lib"
)

# ------------------------------------------------------------------------------
# Main target: built as a DLL, renamed to .asi
# ------------------------------------------------------------------------------
add_library(${PROJECT_NAME} SHARED ${SOURCES})
set_property(TARGET ${PROJECT_NAME} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreadedDebug")
target_link_libraries(${PROJECT_NAME} PRIVATE plugin_sa
    d3d9
    "${PLUGIN_SDK_DIR}/shared/dxsdk/d3dx9.lib")

# Automatically rename the output file extension from .dll to .asi on completion
set_target_properties(${PROJECT_NAME} PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# ------------------------------------------------------------------------------
# Shader compilation (.fx -> .fxo via fxc.exe)
# ------------------------------------------------------------------------------
set(TDU_SHADER_SOURCE_DIR "${TDU_RADAR_ROOT}/gfx/shaders")
set(TDU_SHADER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/tdu-generated/shaders")
set(TDU_SHADER_NAMES circle border image3d line green_square)
set(TDU_SHADER_OUTPUTS)

find_program(FXC_EXECUTABLE NAMES fxc.exe
    PATHS
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x86"
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x86"
)
if(NOT FXC_EXECUTABLE)
    message(FATAL_ERROR "fxc.exe is required to compile The-Definitive-UI radar effects.")
endif()

foreach(SHADER_NAME IN LISTS TDU_SHADER_NAMES)
    set(SHADER_SOURCE "${TDU_SHADER_SOURCE_DIR}/${SHADER_NAME}.fx")
    set(SHADER_OUTPUT "${TDU_SHADER_BUILD_DIR}/${SHADER_NAME}.fxo")
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${TDU_SHADER_BUILD_DIR}"
        COMMAND "${FXC_EXECUTABLE}" /T fx_2_0 /Fo "${SHADER_OUTPUT}" "${SHADER_SOURCE}"
        DEPENDS "${SHADER_SOURCE}"
        VERBATIM
    )
    list(APPEND TDU_SHADER_OUTPUTS "${SHADER_OUTPUT}")
endforeach()

add_custom_target(TduRadarEffects DEPENDS ${TDU_SHADER_OUTPUTS})
add_dependencies(${PROJECT_NAME} TduRadarEffects)

# Copy the compiled shaders next to the .asi, where the mod loads them from at runtime.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/rixgeo.shaders/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${TDU_SHADER_BUILD_DIR}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/rixgeo.shaders/shaders"
    VERBATIM
)
