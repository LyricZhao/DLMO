#pragma once

#include <cstdio>
#include <cstdarg>
#include <cctype>

std::string pretty(size_t value, size_t scale, const char* *units, int m) {
    int count = 0;
    float d = value;
    while (d > scale && count < m - 1) {
        d /= scale;
        count += 1;
    }
    char buffer[256];
    sprintf(buffer, "%.6f %s", d, units[count]);
    return buffer;
}

std::string prettyBytes(size_t size) {
    static const char* units[5] = {"B", "KiB", "MiB", "GiB"};
    return pretty(size, 1024, units, 4);
}

std::string prettyNanoseconds(uint64_t duration) {
    char buffer[256];
    sprintf(buffer, "%.6f ms", duration / 1e6);
    return buffer;
}

void error(const char *fmt, ...) {
    fprintf(stderr, "\033[31mError: ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n\033[0m");
    fflush(stderr);
    std::exit(EXIT_FAILURE);
}

void warning(const char *fmt, ...) {
    fprintf(stderr, "\033[32mWarning: ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n\033[0m");
    fflush(stderr);
}

class Unit {
public:
    template<typename T>
    static constexpr size_t B(T size) {
        return size;
    }

    template<typename T>
    static constexpr size_t KiB(T size) {
        return size * 1024ull;
    }

    template<typename T>
    static constexpr size_t MiB(T size) {
        return size * 1024ull * 1024ull;
    }

    template<typename T>
    static constexpr size_t GiB(T size) {
        return size * 1024ull * 1024ull * 1024ull;
    }

    template<typename T>
    static constexpr uint64_t ns(T time) {
        return time;
    }

    template<typename T>
    static constexpr uint64_t us(T time) {
        return static_cast<uint64_t>(time) * 1000ull;
    }

    template<typename T>
    static constexpr uint64_t ms(T time) {
        return static_cast<uint64_t>(time) * 1000000ull;
    }

    template<typename T>
    static constexpr uint64_t s(T time) {
        return static_cast<uint64_t>(time) * 1000000000ull;
    }

    static size_t fromText(const std::string &text) {
        size_t size = 0;
        int index = 0;
        while (index < text.size() && isdigit(text.at(index))) {
            size = size * 10 + text.at(index ++) - '0';
        }
        if (index == text.size()) {
           error("No unit specified");
        } else if (text.at(index) == 'B') {
            return B(size);
        } else if (text.at(index) == 'K') {
            return KiB(size);
        } else if (text.at(index) == 'M') {
            return MiB(size);
        } else if (text.at(index) == 'G') {
            return GiB(size);
        }
        error("Failed to parse size (format: {num}{B/KiB/MiB/GiB}, e.g. 8GiB)");
        return 0;
    }
};