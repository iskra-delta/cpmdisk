#pragma once

// std::print / std::println landed in GCC 14.  Provide equivalents for
// older toolchains that have std::format (GCC 13+) but not <print>.

#include <cstdio>
#include <format>
#include <string>

namespace pc {

template<typename... Args>
inline void println(std::format_string<Args...> fmt, Args&&... args) {
    std::string s = std::format(fmt, std::forward<Args>(args)...);
    std::puts(s.c_str());
}

template<typename... Args>
inline void println(std::FILE* f, std::format_string<Args...> fmt, Args&&... args) {
    std::string s = std::format(fmt, std::forward<Args>(args)...);
    std::fputs(s.c_str(), f);
    std::fputc('\n', f);
}

// Zero-argument overloads for blank lines.
inline void println()             { std::putchar('\n'); }
inline void println(std::FILE* f) { std::fputc('\n', f); }

} // namespace pc
