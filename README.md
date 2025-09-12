# faircmd — tiny C++ command script runners (header-only)

faircmd is a header-only set of C++ “script runners” for driving deterministic and interactive test harnesses. It provides three modes: machine (strict FIFO, CI-friendly), hybrid (stdin + queue for demos), and split (bag-of-tokens when order doesn’t matter). Cross-platform, no deps, with examples and CMake.

**Why:** Driving concurrent tests with exact, human-readable scripts is painful in C++.
**faircmd** gives you three tiny runners:

- `machine` — strict FIFO, deterministic (CI default)
- `hybrid`  — stdin + queue for interactive demos
- `split`   — presence/not-order bag for availability checks

```cpp
#define FAIRCMD_MODE 2     // 0=hybrid, 1=split, 2=machine (default)
#include "faircmd.hpp"

faircmd::preload({"go","promote","stop"});
faircmd::WaitForCommand("worker","go");
```

## Install
Header-only. Copy `include/` or add as a submodule and:

```cmake
add_subdirectory(faircmd)
target_link_libraries(your_tests PRIVATE faircmd)
```

Or via `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
  faircmd
  GIT_REPOSITORY https://github.com/USER/faircmd.git
  GIT_TAG v0.1.0 # or main
)
FetchContent_MakeAvailable(faircmd)
```

## Guarantees
- **machine:** only consumes when `expected == front()`, cooperative blocking, per-item fail budget.
- **hybrid:** reads from stdin *and* queue; not strictly FIFO.
- **split:** bag semantics; satisfies when matching token exists.

## Tips
- Send diagnostics to `std::cerr` so you don’t corrupt structured output.
- For concurrency bugs, run tests under TSAN.
- Keep your test harness deterministic by preferring the `machine` variant.

## License
MIT
