#pragma once

#include <iostream>

inline int g_failed = 0;

inline void check(bool cond, const char* expr) {
    if (!cond) {
        std::cerr << "FAIL: " << expr << '\n';
        ++g_failed;
    }
}

#define CHECK(x) check((x), #x)
