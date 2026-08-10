cmake_minimum_required(VERSION 3.25)

project(proton-inject VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

set(WINDOWS_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/windows")
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${WINDOWS_ARTIFACT_DIR}")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

set(MINGW_CXX x86_64-w64-mingw32-g++)
find_program(MINGW_CXX_COMPILER ${MINGW_CXX})
find_program(MINGW_STRIP x86_64-w64-mingw32-strip)
find_program(OBJCOPY objcopy)

set(EMBEDDED_OBJECTS)

if(MINGW_CXX_COMPILER AND OBJCOPY)
    set(LOADER_DLL "${WINDOWS_ARTIFACT_DIR}/loader.dll")
    set(INJECTOR_EXE "${WINDOWS_ARTIFACT_DIR}/injector.exe")
    set(LOADER_EMBED "${GENERATED_DIR}/loader.dll")
    set(INJECTOR_EMBED "${GENERATED_DIR}/injector.exe")
    set(LOADER_OBJ "${GENERATED_DIR}/loader.o")
    set(INJECTOR_OBJ "${GENERATED_DIR}/injector.o")

    add_custom_command(
        OUTPUT ${LOADER_DLL}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${WINDOWS_ARTIFACT_DIR}
        COMMAND
            ${MINGW_CXX_COMPILER} -shared -O2 -std=c++23 -static
            -DWIN32_LEAN_AND_MEAN -DNOMINMAX
            ${CMAKE_SOURCE_DIR}/windows/loader/dllmain.cpp
            ${CMAKE_SOURCE_DIR}/windows/loader/mod_loader.cpp
            -o ${LOADER_DLL}
            -luser32 -lshell32 -lole32
        DEPENDS
            ${CMAKE_SOURCE_DIR}/windows/loader/dllmain.cpp
            ${CMAKE_SOURCE_DIR}/windows/loader/mod_loader.cpp
        COMMENT "Building loader.dll (MinGW)"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${INJECTOR_EXE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${WINDOWS_ARTIFACT_DIR}
        COMMAND
            ${MINGW_CXX_COMPILER} -O2 -std=c++23 -static
            -DWIN32_LEAN_AND_MEAN -DNOMINMAX
            ${CMAKE_SOURCE_DIR}/windows/injector/main.cpp
            ${CMAKE_SOURCE_DIR}/windows/injector/inject.cpp
            ${CMAKE_SOURCE_DIR}/windows/injector/method.cpp
            ${CMAKE_SOURCE_DIR}/windows/injector/procutil.cpp
            ${CMAKE_SOURCE_DIR}/windows/injector/remote_module.cpp
            -o ${INJECTOR_EXE}
            -luser32 -lkernel32 -lpsapi -ladvapi32
        DEPENDS
            ${CMAKE_SOURCE_DIR}/windows/injector/main.cpp
            ${CMAKE_SOURCE_DIR}/windows/injector/inject.cpp
            ${CMAKE_SOURCE_DIR}/windows/injector/method.cpp
            ${CMAKE_SOURCE_DIR}/windows/injector/procutil.cpp
            ${CMAKE_SOURCE_DIR}/windows/injector/remote_module.cpp
        COMMENT "Building injector.exe (MinGW)"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${LOADER_OBJ}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${LOADER_DLL} ${LOADER_EMBED}
        COMMAND ${MINGW_STRIP} --strip-unneeded ${LOADER_EMBED}
        COMMAND bash -c
            "cd '${GENERATED_DIR}' && ${OBJCOPY} -I binary -O elf64-x86-64 -B i386 \
            --rename-section .data=.rodata,alloc,load,readonly,data,contents \
            loader.dll loader.o"
        DEPENDS ${LOADER_DLL} ${MINGW_STRIP}
        COMMENT "Embedding loader.dll"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${INJECTOR_OBJ}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${INJECTOR_EXE} ${INJECTOR_EMBED}
        COMMAND ${MINGW_STRIP} --strip-unneeded ${INJECTOR_EMBED}
        COMMAND bash -c
            "cd '${GENERATED_DIR}' && ${OBJCOPY} -I binary -O elf64-x86-64 -B i386 \
            --rename-section .data=.rodata,alloc,load,readonly,data,contents \
            injector.exe injector.o"
        DEPENDS ${INJECTOR_EXE} ${MINGW_STRIP}
        COMMENT "Embedding injector.exe"
        VERBATIM
    )

    list(APPEND EMBEDDED_OBJECTS ${LOADER_OBJ} ${INJECTOR_OBJ})
    add_custom_target(embedded_objects DEPENDS ${LOADER_OBJ} ${INJECTOR_OBJ})
    add_custom_target(windows_artifacts DEPENDS ${LOADER_DLL} ${INJECTOR_EXE})
else()
    message(WARNING "MinGW or objcopy not found; Windows PE blobs will be empty stubs")
endif()

set(SOURCES
    src/main.cpp
    src/cli/cli.cpp
    src/config/config.cpp
    src/core/injector.cpp
    src/core/method.cpp
    src/core/embedded_assets.cpp
    src/proton/proton.cpp
    src/proton/vdf.cpp
    src/utils/utils.cpp
    src/gui/application.cpp
    src/gui/app_icon.cpp
    src/gui/qt/form.cpp
    src/gui/qt/dialogs.cpp
    src/gui/qt/main_window.cpp
    src/gui/inject_runner.cpp
    src/gui/steam_service.cpp
    src/gui/mods_directory.cpp
    src/gui/profile_manager.cpp
    src/gui/history_manager.cpp
    ${EMBEDDED_OBJECTS}
)

add_executable(proton-inject ${SOURCES})
set_target_properties(proton-inject PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(TARGET embedded_objects)
    add_dependencies(proton-inject embedded_objects)
endif()

target_include_directories(proton-inject PRIVATE src src/gui src/gui/qt ${GENERATED_DIR} ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(proton-inject PRIVATE PROTON_INJECT_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_link_libraries(proton-inject PRIVATE Qt6::Widgets)
target_compile_options(proton-inject PRIVATE -Wall -Wextra -Wpedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(proton-inject PRIVATE -g)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(proton-inject PRIVATE -O2 -DNDEBUG)
    add_custom_command(
        TARGET proton-inject
        POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:proton-inject>
        COMMENT "Stripping release binary"
        VERBATIM
    )
endif()

install(TARGETS proton-inject RUNTIME DESTINATION bin)
