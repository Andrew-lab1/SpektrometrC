// Wrapper header to make the generated Qt UI header available under a stable include
// path regardless of where `uic` places it in the build output.
#pragma once

#if __has_include("x64/Debug/qt/uic/ui_Spektrometr.h")
#include "x64/Debug/qt/uic/ui_Spektrometr.h"
#elif __has_include("x64/Release/qt/uic/ui_Spektrometr.h")
#include "x64/Release/qt/uic/ui_Spektrometr.h"
#elif __has_include("qt/uic/ui_Spektrometr.h")
#include "qt/uic/ui_Spektrometr.h"
#else
#error "Cannot find generated ui_Spektrometr.h. Ensure Qt uic runs before building."
#endif
