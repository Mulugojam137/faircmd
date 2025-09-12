#include <iostream>

#ifndef FAIRCMD_MODE
#define FAIRCMD_MODE 0  // default to hybrid when not overridden by the build
#endif
#include "faircmd.hpp"

int main() {
  faircmd::reset();
  faircmd::set_default_fails(100000);
  faircmd::set_yield_sleep(std::chrono::milliseconds(5));
  faircmd::preload({"hello"});
  std::cout << "Type tokens then enter; waiting for 'hello' then 'world'..." << std::endl;
  faircmd::start_stdin_pumper();
  faircmd::WaitForCommand("demo","hello");
  std::cout << "hello!" << std::endl;
  faircmd::WaitForCommand("demo","world");
  std::cout << "world!" << std::endl;
  faircmd::stop_stdin_pumper();
}
