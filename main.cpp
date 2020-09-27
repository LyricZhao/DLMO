#include <iostream>
#include <memory>

#include "runner.hpp"
#include "utils.hpp"

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "Usage: dlmo <input> <output> <limit>" << std::endl;
        exit(0);
    }

    // Run cases
    std::string input(argv[1]), output(argv[2]), limit(argv[3]);
    auto runner = Runner(input, output, Unit::fromText(limit));
    runner.run();

    return 0;
}
