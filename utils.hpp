#pragma once

#include <cstdio>

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
    static const char* units[5] = {"Bytes", "KBytes", "MBytes", "GBytes"};
    return pretty(size, 1024, units, 4);
}

std::string prettyNanosecond(uint64_t duration) {
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
    static size_t B(T size) {
        return size;
    }

    template<typename T>
    static size_t KB(T size) {
        return size * 1024;
    }

    template<typename T>
    static size_t MB(T size) {
        return size * 1024 * 1024;
    }

    template<typename T>
    static size_t GB(T size) {
        return size * 1024 * 1024 * 1024;
    }

    static size_t from(const std::string &text) {
        size_t size = 0;
        int index = 0;
        while (index < text.size() && isnumber(text.at(index))) {
            size = size * 10 + text.at(index ++) - '0';
        }
        if (index == text.size()) {
           error("No unit specified");
        } else if (text.at(index) == 'B') {
            return B(size);
        } else if (text.at(index) == 'K') {
            return KB(size);
        } else if (text.at(index) == 'M') {
            return MB(size);
        } else if (text.at(index) == 'G') {
            return GB(size);
        }
        error("Failed to parse size (format: {num}{B/KB/MB/GB}, e.g. 8GB)");
        return 0;
    }
};