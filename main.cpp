#include <iostream>
#include <memory>
#include <fstream>

#include "runner.hpp"
#include "utils.hpp"

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: dlmo <path_to_config>" << std::endl;
        exit(0);
    }

    // Run cases
    std::ifstream config(argv[1]);
    std::string path, limit;
    bool use_recompute, use_swap;
    while (config >> path >> limit >> use_recompute >> use_swap) {
        auto runner = Runner(path, Unit::fromText(limit), use_recompute, use_swap);
        runner.run();
    }

    return 0;
}
