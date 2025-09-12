#include <iostream>
#define FAIRCMD_MODE 2
#include "faircmd.hpp"

int main() {
  faircmd::reset();
  faircmd::set_default_fails(1000);
  faircmd::set_yield_sleep(std::chrono::milliseconds(0));

  faircmd::preload({"go","promote","stop"});

  std::cout << "worker waiting..." << std::endl;
  faircmd::WaitForCommand("worker","go");
  std::cout << "go" << std::endl;
  faircmd::WaitForCommand("worker","promote");
  std::cout << "promote" << std::endl;
  faircmd::WaitForCommand("worker","stop");
  std::cout << "stop" << std::endl;
  return 0;
}
