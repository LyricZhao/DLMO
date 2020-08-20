#pragma once

#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <random>

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
        return time * 1000ull;
    }

    template<typename T>
    static constexpr uint64_t ms(T time) {
        return time * 1000000ull;
    }

    template<typename T>
    static constexpr uint64_t s(T time) {
        return time * 1000000000ull;
    }

    static size_t fromText(const std::string &text) {
        const char *ptr = text.c_str();
        char *unit_ptr;
        double size = strtod(ptr, &unit_ptr);
        if (unit_ptr - ptr == text.size()) {
           error("No unit specified");
        } else if (*unit_ptr == 'B') {
            return B(size);
        } else if (*unit_ptr == 'K') {
            return KiB(size);
        } else if (*unit_ptr == 'M') {
            return MiB(size);
        } else if (*unit_ptr == 'G') {
            return GiB(size);
        }
        error("Failed to parse size (format: {num}{B/KiB/MiB/GiB}, e.g. 8GiB)");
        return 0;
    }
};

class Random {
    std::default_random_engine engine;
    std::uniform_int_distribution<int> dist;

public:
    // Random an int in [min, max)
    Random(int min, int max, int seed=0) {
        if (min >= max) {
            error("Min value should be less than max.\n");
        }
        engine = std::default_random_engine(seed);
        dist = std::uniform_int_distribution<int>(min, max - 1);
    }

    int operator () () {
        return dist(engine);
    }
};

#define LOOP(var, list_head) for (auto (var) = (list_head); (var); (var) = (var)->next)
#define LOOP_BACK(var, list_tail) for (auto (var) = (list_tail); (var); (var) = (var)->prev)