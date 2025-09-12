#include <cassert>
#define FAIRCMD_MODE 2
#include "faircmd.hpp"

int main() {
  faircmd::reset();
  faircmd::set_default_fails(100);
  faircmd::set_yield_sleep(std::chrono::milliseconds(0));
  faircmd::preload({"a","b","c"});
  faircmd::WaitForCommand("t","a");
  faircmd::WaitForCommand("t","b");
  faircmd::WaitForCommand("t","c");
  return 0;
}
