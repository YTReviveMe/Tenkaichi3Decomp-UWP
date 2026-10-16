cmake_minimum_required(VERSION 3.26)
project(bt3_uwp_storage LANGUAGES C CXX)

add_library(bt3_uwp_storage STATIC storage.cpp storage_winrt.cpp audio.cpp http.cpp iso_io.cpp installer.cpp ../setup/native.cpp)
target_compile_features(bt3_uwp_storage PUBLIC cxx_std_17)
target_compile_definitions(bt3_uwp_storage PUBLIC BT3_UWP=1)
target_include_directories(bt3_uwp_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(FATAL_ERROR "This target requires the UWP toolchain")
endif()

set(BT3_SDL3_UWP_SOURCE "" CACHE PATH "Path to the SDL3 UWP source tree")
set(BT3_SDL3_UWP_BUILD "" CACHE PATH "Path to the patched SDL3 UWP CMake build")
if(BT3_SDL3_UWP_SOURCE)
    target_include_directories(bt3_uwp_storage PRIVATE ${BT3_SDL3_UWP_SOURCE}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/imgui)
endif()
option(BT3_GAME "Link the full game from UWP COFF objects" OFF)
if(BT3_GAME)
    if(NOT BT3_SDL3_UWP_SOURCE OR NOT BT3_SDL3_UWP_BUILD)
        message(FATAL_ERROR "The game needs the patched SDL source and build paths")
    endif()
    set(package_assets
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/StoreLogo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/Square150x150Logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/Square44x44Logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/SplashScreen.png)
    file(GLOB tbo_assets CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../tbo/ui/*.png")
    list(APPEND tbo_assets "${CMAKE_CURRENT_SOURCE_DIR}/../tbo/ui/CREDITS.md")
    file(GLOB_RECURSE font_assets CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/fonts/*")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tbo/ui/bg.png" OR
       NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/fonts/fonts.tsv")
        message(FATAL_ERROR "Tenkaichi Budokai Online artwork and fonts are required in the Xbox package")
    endif()
    set_source_files_properties(${tbo_assets} PROPERTIES
        VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "tbo/ui")
    foreach(font_asset IN LISTS font_assets)
        file(RELATIVE_PATH font_relative "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/fonts" "${font_asset}")
        get_filename_component(font_subdir "${font_relative}" DIRECTORY)
        if(font_subdir)
            set(font_location "fonts/${font_subdir}")
        else()
            set(font_location "fonts")
        endif()
        set_source_files_properties("${font_asset}" PROPERTIES
            VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "${font_location}")
    endforeach()
    set(sdl_dll "${BT3_SDL3_UWP_BUILD}/Release/SDL3.dll")
    set(data_manifest "${CMAKE_CURRENT_SOURCE_DIR}/../build/uwp/pack/Tenkaichi3DecompUWP.dat")
    set_source_files_properties(${package_assets} PROPERTIES
        VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION assets)
    set_source_files_properties("${sdl_dll}" PROPERTIES
        VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION ".")
    if(EXISTS "${data_manifest}")
        set_source_files_properties("${data_manifest}" PROPERTIES
            VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION ".")
    else()
        message(WARNING "The full game package has no Tenkaichi3DecompUWP.dat yet; run make_uwp_dat.py")
    endif()
    file(GLOB game_objects "${CMAKE_CURRENT_SOURCE_DIR}/../build/uwp/game/*.obj")
    file(GLOB host_objects "${CMAKE_CURRENT_SOURCE_DIR}/../build/uwp/host/*.obj")
    file(GLOB data_objects "${CMAKE_CURRENT_SOURCE_DIR}/../build/uwp/data/*.obj")
    if(NOT game_objects OR NOT host_objects OR NOT data_objects)
        message(FATAL_ERROR "Run the UWP game and host object builders first")
    endif()
    add_executable(bt3_uwp_game WIN32 game_entry.cpp GamePackage.appxmanifest
                   ${package_assets} ${tbo_assets} ${font_assets} "${sdl_dll}" "${data_manifest}")
    set_target_properties(bt3_uwp_game PROPERTIES OUTPUT_NAME Tenkaichi3DecompUWP)
    set_property(TARGET bt3_uwp_game PROPERTY VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.19041.0")
    set_source_files_properties(game_entry.cpp PROPERTIES COMPILE_OPTIONS /ZW)
    target_include_directories(bt3_uwp_game PRIVATE ${BT3_SDL3_UWP_SOURCE}/include)
    target_link_libraries(bt3_uwp_game PRIVATE bt3_uwp_storage vccorlib WindowsApp
        "${BT3_SDL3_UWP_BUILD}/Release/SDL3.lib" ${game_objects} ${host_objects} ${data_objects})
    target_link_options(bt3_uwp_game PRIVATE /APPCONTAINER /WINMD
        "/WINMDFILE:${CMAKE_CURRENT_BINARY_DIR}/Tenkaichi3DecompUWP.winmd"
        "/MAP:${CMAKE_CURRENT_BINARY_DIR}/Tenkaichi3DecompUWP.map"
        /BASE:0x20000000 /DYNAMICBASE /HIGHENTROPYVA:NO /LARGEADDRESSAWARE:NO /STACK:0x1000000)
    # ADDR32 tables require /LARGEADDRESSAWARE:NO at link time; restore it afterward.
    get_filename_component(bt3_linker_dir "${CMAKE_LINKER}" DIRECTORY)
    find_program(bt3_editbin NAMES editbin.exe PATHS "${bt3_linker_dir}" NO_DEFAULT_PATH REQUIRED)
    add_custom_command(TARGET bt3_uwp_game POST_BUILD
        COMMAND "${bt3_editbin}" /NOLOGO /LARGEADDRESSAWARE "$<TARGET_FILE:bt3_uwp_game>"
        COMMENT "Allow full x64 process address space while retaining low game image")
endif()

if(BT3_SDL3_UWP_SOURCE)
    add_library(bt3_uwp_renderer_probe OBJECT ../src/gs/gs_gpu.c)
    target_compile_definitions(bt3_uwp_renderer_probe PRIVATE BT3_UWP=1 _CRT_SECURE_NO_WARNINGS)
    target_include_directories(bt3_uwp_renderer_probe PRIVATE
        ${BT3_SDL3_UWP_SOURCE}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/gen/gs
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/gs)
endif()

option(BT3_BOOTSTRAP "Build an Xbox platform check executable" OFF)
if(BT3_BOOTSTRAP)
    if(NOT BT3_SDL3_UWP_SOURCE OR NOT BT3_SDL3_UWP_BUILD)
        message(FATAL_ERROR "The platform check needs the patched SDL source and build paths")
    endif()
    set(package_assets
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/StoreLogo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/Square150x150Logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/Square44x44Logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/SplashScreen.png)
    set(sdl_dll "${BT3_SDL3_UWP_BUILD}/Release/SDL3.dll")
    set_source_files_properties(${package_assets} PROPERTIES
        VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION assets)
    set_source_files_properties("${sdl_dll}" PROPERTIES
        VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION ".")
    add_executable(bt3_uwp_bootstrap WIN32 bootstrap.cpp Package.appxmanifest ${package_assets} "${sdl_dll}")
    set_target_properties(bt3_uwp_bootstrap PROPERTIES OUTPUT_NAME Tenkaichi3DecompUWP)
    set_property(TARGET bt3_uwp_bootstrap PROPERTY VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.19041.0")
    set_source_files_properties(bootstrap.cpp PROPERTIES COMPILE_OPTIONS /ZW)
    target_include_directories(bt3_uwp_bootstrap PRIVATE ${BT3_SDL3_UWP_SOURCE}/include)
    target_link_libraries(bt3_uwp_bootstrap PRIVATE bt3_uwp_storage WindowsApp
        "${BT3_SDL3_UWP_BUILD}/Release/SDL3.lib")
    target_link_options(bt3_uwp_bootstrap PRIVATE /APPCONTAINER /WINMD
        "/WINMDFILE:${CMAKE_CURRENT_BINARY_DIR}/Tenkaichi3DecompUWP.winmd")
endif()
