cmake_minimum_required(VERSION 3.20)
project(AventicsToolbox VERSION 0.5.2 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Aventics Toolbox v0.5.2 targets Creo Parametric on Windows x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

set(PROTOOLKIT_INCLUDE_DIR "" CACHE PATH "Creo TOOLKIT include directory containing ProToolkit.h")
set(PROTOOLKIT_LIB_DIR "" CACHE PATH "Directory containing protk_dll_NU.lib")
set(CREO_COMMON_LIB_DIR "" CACHE PATH "Directory containing ucore.lib and udata.lib; often the same as PROTOOLKIT_LIB_DIR")
option(AVENTICS_TYPESCRIPT_UI "Build the experimental TypeScript/WebView2 UI" ON)
set(WEBVIEW2_SDK_DIR "" CACHE PATH "Root of the Microsoft.Web.WebView2 NuGet package")

if(NOT EXISTS "${PROTOOLKIT_INCLUDE_DIR}/ProToolkit.h")
    message(FATAL_ERROR "Set PROTOOLKIT_INCLUDE_DIR to the Creo TOOLKIT include directory containing ProToolkit.h.")
endif()
if(NOT EXISTS "${PROTOOLKIT_LIB_DIR}/protk_dll_NU.lib")
    message(FATAL_ERROR "Set PROTOOLKIT_LIB_DIR to the Creo TOOLKIT library directory containing protk_dll_NU.lib.")
endif()
if(NOT EXISTS "${CREO_COMMON_LIB_DIR}/ucore.lib")
    message(FATAL_ERROR "Set CREO_COMMON_LIB_DIR to a directory containing ucore.lib.")
endif()
if(NOT EXISTS "${CREO_COMMON_LIB_DIR}/udata.lib")
    message(FATAL_ERROR "Set CREO_COMMON_LIB_DIR to a directory containing udata.lib.")
endif()

add_library(AventicsToolbox SHARED
    src/app/main.cpp
    src/app/Commands.cpp
    src/app/ToolboxDialogV050.cpp
    src/app/AppContext.cpp

    src/common/ModelUtils.cpp
    src/common/FolderScanner.cpp
    src/common/FolderPicker.cpp
    src/common/SelectionService.cpp
    src/common/SessionSnapshot.cpp
    src/common/ModelLoader.cpp
    src/common/Logger.cpp
    src/common/UiUtils.cpp
    src/common/OperationRunner.cpp
    src/common/FamilyTableService.cpp
    src/common/StepImporter.cpp

    src/tools/weak_dimension/WeakDimensionChecker.cpp
    src/tools/accuracy/AccuracyChecker.cpp
    src/tools/inspection/PlacementEngine.cpp
    src/tools/inspection/InspectionBuilder.cpp
    src/tools/instance_builder/InstanceBuilder.cpp
    src/tools/instance_builder/InstanceBuilderUi.cpp
)

target_include_directories(AventicsToolbox PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROTOOLKIT_INCLUDE_DIR}"
)

target_compile_definitions(AventicsToolbox PRIVATE
    PRO_MACHINE=36
    PRO_OS=4
    PRO_USE_VAR_ARGS
    _CRT_SECURE_NO_WARNINGS
    _UNICODE
    UNICODE
    NOMINMAX
)

target_link_directories(AventicsToolbox PRIVATE
    "${PROTOOLKIT_LIB_DIR}"
    "${CREO_COMMON_LIB_DIR}"
)

target_link_libraries(AventicsToolbox PRIVATE
    protk_dll_NU.lib
    ucore.lib
    udata.lib

    kernel32.lib
    user32.lib
    wsock32.lib
    advapi32.lib
    mpr.lib
    winspool.lib
    netapi32.lib
    psapi.lib
    gdi32.lib
    shell32.lib
    comdlg32.lib
    ole32.lib
    uuid.lib
    ws2_32.lib
)

if(AVENTICS_TYPESCRIPT_UI)
    if(NOT EXISTS "${WEBVIEW2_SDK_DIR}/build/native/include/WebView2.h")
        message(FATAL_ERROR
            "AVENTICS_TYPESCRIPT_UI is ON. Set WEBVIEW2_SDK_DIR to the extracted Microsoft.Web.WebView2 NuGet package root.")
    endif()

    set(WEBVIEW2_LOADER_DLL "")
    foreach(candidate
        "${WEBVIEW2_SDK_DIR}/build/native/x64/WebView2Loader.dll"
        "${WEBVIEW2_SDK_DIR}/runtimes/win-x64/native/WebView2Loader.dll")
        if(EXISTS "${candidate}")
            set(WEBVIEW2_LOADER_DLL "${candidate}")
            break()
        endif()
    endforeach()
    if(NOT WEBVIEW2_LOADER_DLL)
        message(FATAL_ERROR "Could not locate the x64 WebView2Loader.dll under WEBVIEW2_SDK_DIR.")
    endif()

    find_program(NPM_EXECUTABLE NAMES npm npm.cmd REQUIRED)
    set(AVENTICS_WEB_UI_JS "${CMAKE_CURRENT_SOURCE_DIR}/ui/dist/app.js")
    add_custom_command(
        OUTPUT "${AVENTICS_WEB_UI_JS}"
        COMMAND "${NPM_EXECUTABLE}" install --no-audit --no-fund
        COMMAND "${NPM_EXECUTABLE}" run build
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ui"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/ui/package.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/ui/tsconfig.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/ui/src/app.ts"
        COMMENT "Building Aventics TypeScript UI"
        VERBATIM
    )
    add_custom_target(AventicsWebUi DEPENDS "${AVENTICS_WEB_UI_JS}")
    add_dependencies(AventicsToolbox AventicsWebUi)

    target_sources(AventicsToolbox PRIVATE src/app/WebToolbox.cpp)
    target_include_directories(AventicsToolbox PRIVATE "${WEBVIEW2_SDK_DIR}/build/native/include")
    target_compile_definitions(AventicsToolbox PRIVATE AVENTICS_TYPESCRIPT_UI=1)
    target_link_libraries(AventicsToolbox PRIVATE shlwapi.lib)
endif()

# This matches the protk_dll_NU configuration that successfully built the
# v0.1.0 checker on the target Creo workstation. If your PTC sample
# makefile uses the MD variant, switch both runtime and Toolkit library.
set_property(TARGET AventicsToolbox PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_options(AventicsToolbox PRIVATE /W4 /permissive- /EHsc)

set_target_properties(AventicsToolbox PROPERTIES
    OUTPUT_NAME "aventics_toolbox"
    PREFIX ""
)

add_custom_command(TARGET AventicsToolbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist/x86e_win64/obj"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:AventicsToolbox>"
        "${CMAKE_CURRENT_SOURCE_DIR}/dist/x86e_win64/obj/$<TARGET_FILE_NAME:AventicsToolbox>"
)

if(AVENTICS_TYPESCRIPT_UI)
    add_custom_command(TARGET AventicsToolbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist/x86e_win64/obj/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBVIEW2_LOADER_DLL}"
            "${CMAKE_CURRENT_SOURCE_DIR}/dist/x86e_win64/obj/WebView2Loader.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ui/index.html"
            "${CMAKE_CURRENT_SOURCE_DIR}/dist/x86e_win64/obj/ui/index.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ui/styles.css"
            "${CMAKE_CURRENT_SOURCE_DIR}/dist/x86e_win64/obj/ui/styles.css"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AVENTICS_WEB_UI_JS}"
            "${CMAKE_CURRENT_SOURCE_DIR}/dist/x86e_win64/obj/ui/app.js"
    )
endif()
