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

    if (WIN32)
        # The exact library name can vary by SDK version.
        # Prefer an explicit path via PIXELINK_SDK_LIB if this doesn't match.
        set(PIXELINK_SDK_LIB "" CACHE FILEPATH "Path to PixeLINK import library (.lib) (optional)")
        if (PIXELINK_SDK_LIB)
            target_link_libraries(Spektrometr PRIVATE "${PIXELINK_SDK_LIB}")
        else()
            message(STATUS "PIXELINK_SDK_ROOT set, but PIXELINK_SDK_LIB not provided; build will compile only if headers suffice.")
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
