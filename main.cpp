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
    bool recompute_on, swap_on;
    while (config >> path >> limit >> recompute_on >> swap_on) {
        auto runner = Runner(path, Unit::from(limit), recompute_on, swap_on);
        runner.run();
    }

    return 0;
}
