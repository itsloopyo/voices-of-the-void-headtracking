// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdlib>
#include <string>

// Reading a number out of a hand-typed command line.
//
// strtol / strtod, never std::stoi / std::stof: the text comes out of
// HeadTracking.devcmd, and the throwing forms turn a typo into an uncaught C++
// exception on the game thread inside the render detour, which takes the game
// with it.
//
// Pure and header-only, so the rules below run in tests with no game.
namespace votv_ht::dev_parse {

// Trailing blanks are fine - a hand-typed command line collects them - but
// nothing else may follow the number, and an empty parse is not a number.
inline bool NothingButBlanksLeft(const char* start, const char* end) {
    if (end == start) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
    return *end == 0;
}

inline bool ParseInt(const std::string& text, long& out) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (!NothingButBlanksLeft(text.c_str(), end)) return false;
    out = value;
    return true;
}

inline bool ParseDouble(const std::string& text, double& out) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (!NothingButBlanksLeft(text.c_str(), end)) return false;
    out = value;
    return true;
}

}  // namespace votv_ht::dev_parse
