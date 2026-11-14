#pragma once
// Stop <windows.h> (pulled in indirectly) from defining min/max macros.
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
// CommonLib's generated plugin declaration uses string-view literals.
using namespace std::literals;
