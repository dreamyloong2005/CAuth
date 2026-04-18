#include "cli/steam_cli.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        return cauth::cli::run_cli(argc, argv, std::cout, std::cerr);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Fatal error: unknown exception\n";
        return 1;
    }
}
