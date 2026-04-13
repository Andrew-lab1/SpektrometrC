cmake_minimum_required(VERSION 3.16)

project(Spektrometr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 QUIET COMPONENTS Widgets SerialPort)
if (Qt6_FOUND)
    set(QT_MAJOR 6)
    set(QT_LIBS Qt6::Widgets Qt6::SerialPort)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets SerialPort)
    set(QT_MAJOR 5)
    set(QT_LIBS Qt5::Widgets Qt5::SerialPort)
endif()

add_executable(Spektrometr
    Spektrometr/main.cpp
    Spektrometr/Spektrometr.cpp
    Spektrometr/Spektrometr.h
    Spektrometr/HeatmapWindow.cpp
    Spektrometr/HeatmapWindow.h
    Spektrometr/Options.cpp
    Spektrometr/Options.h
    Spektrometr/Spektrometr.ui
)

target_link_libraries(Spektrometr PRIVATE ${QT_LIBS})

if (WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS
            "${Qt6_DIR}/../../../bin"
            "${Qt5_DIR}/../../../bin"
    )

    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Spektrometr POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --compiler-runtime "$<TARGET_FILE:Spektrometr>"
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:Spektrometr>"
            COMMENT "Deploying Qt runtime"
        )
    endif()
endif()

# Optional: PixeLINK SDK (headers+libs) if provided.
# Usage example:
#   cmake -S . -B build \
#     -DPIXELINK_SDK_ROOT=/path/to/pixelink
# expecting:
#   ${PIXELINK_SDK_ROOT}/include/PixeLINKApi.h
#   ${PIXELINK_SDK_ROOT}/lib/(PxLApi | PxLApi64 | etc.)
set(PIXELINK_SDK_ROOT "" CACHE PATH "Path to PixeLINK SDK root (optional)")
if (PIXELINK_SDK_ROOT)
    target_include_directories(Spektrometr PRIVATE "${PIXELINK_SDK_ROOT}/include")

    set(PIXELINK_RUNTIME_DIR "${PIXELINK_SDK_ROOT}/bin" CACHE PATH "Path to PixeLINK runtime DLLs (optional)")

    if (WIN32)
        # The exact library name can vary by SDK version.
        # Prefer an explicit path via PIXELINK_SDK_LIB if this doesn't match.
        set(PIXELINK_SDK_LIB "" CACHE FILEPATH "Path to PixeLINK import library (.lib) (optional)")
        if (PIXELINK_SDK_LIB)
            target_link_libraries(Spektrometr PRIVATE "${PIXELINK_SDK_LIB}")
        else()
            message(STATUS "PIXELINK_SDK_ROOT set, but PIXELINK_SDK_LIB not provided; build will compile only if headers suffice.")
        endif()

        if (PIXELINK_RUNTIME_DIR AND EXISTS "${PIXELINK_RUNTIME_DIR}")
            file(GLOB PIXELINK_RUNTIME_DLLS CONFIGURE_DEPENDS "${PIXELINK_RUNTIME_DIR}/*.dll")
            if (PIXELINK_RUNTIME_DLLS)
                add_custom_command(TARGET Spektrometr POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PIXELINK_RUNTIME_DLLS} "$<TARGET_FILE_DIR:Spektrometr>"
                    COMMENT "Copying PixeLINK runtime DLLs"
                )
            endif()
        endif()
    else()
        # Linux: link with -lPxLApi if present in SDK lib dir
        target_link_directories(Spektrometr PRIVATE "${PIXELINK_SDK_ROOT}/lib")
        # libPxLApi.so -> -lPxLApi -> CMake name: PxLApi
        target_link_libraries(Spektrometr PRIVATE PxLApi)
        # Help runtime find the bundled .so if you copy it next to the executable (./) or into ./lib
        set_target_properties(Spektrometr PROPERTIES
            BUILD_RPATH "$ORIGIN;$ORIGIN/lib"
            INSTALL_RPATH "$ORIGIN;$ORIGIN/lib")
    endif()
endif()
