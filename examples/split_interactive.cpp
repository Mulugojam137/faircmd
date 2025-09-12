// examples/split_interactive.cpp

#ifndef FAIRCMD_MODE
#define FAIRCMD_MODE 1  // split default; CMake can override
#endif
#include "faircmd.hpp"

#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

int main() {
    faircmd::reset();
    faircmd::set_yield_sleep(std::chrono::milliseconds(1)); // be gentle to CPU

    // Read tokens from stdin and push into the split "bag"
    std::thread reader([] {
        std::string line, tok;
        while (std::getline(std::cin, line)) {
            std::istringstream iss(line);
            while (iss >> tok) faircmd::push(tok);
        }
        });
    reader.detach(); // process exit will end it

    try {
        faircmd::WaitForCommand("split_demo", "hello");
        faircmd::WaitForCommand("split_demo", "world");
        std::cout << "OK (got hello + world)\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        faircmd::dump_pending_to_stderr();
        return 1;
    }
    return 0;
}
