// windows.h the way this project wants it: without the rarely used parts and without the
// min/max macros (they break std::min/std::max). Include this instead of <windows.h>.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
