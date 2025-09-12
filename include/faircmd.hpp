#pragma once
#ifndef FAIRCMD_MODE
  #define FAIRCMD_MODE 2  // 0=hybrid, 1=split, 2=machine (default)
#endif

#if FAIRCMD_MODE == 0
  #include "faircmd_hybrid.hpp"
  namespace faircmd = faircmd_hybrid;
#elif FAIRCMD_MODE == 1
  #include "faircmd_split.hpp"
  namespace faircmd = faircmd_split;
#elif FAIRCMD_MODE == 2
  #include "faircmd_machine.hpp"
  namespace faircmd = faircmd_machine;
#else
  #error "Invalid FAIRCMD_MODE"
#endif
