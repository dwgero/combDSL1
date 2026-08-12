### 🚨 Current Focus & Active Blockers
- **Context:** The root INSTALL guide and CMake configuration now document and verify the complete native, test and Emscripten build toolchains.
- **Next Step:** No active blocker. Native and Emscripten builds and all 24 tests pass; the build-requirements work is complete on main.

### 🛠️ Architectural Discoveries (Codex Insights)
- Ordinary Studio evaluation must run in bounded Web Worker slices so a Pause message can be handled between slices.
- Automatic Single Step and Key Step already have resumable retained state; manual Pause must retain that state without resetting the configured step-limit window.
- Find remains synchronous and non-cooperative. Pause stops its worker, and Resume restores definitions and restarts the same search in the same Results entry.
- Internal C++ slice yields must skip whole-expression redex scans and serialization; those are only needed for completion or a real user-visible step-limit boundary.
- The effective CMake minimum is 3.20 because the build uses string(JSON), FindPython3 and REQUIRED find_program behavior unavailable in CMake 3.2.
- Full native CREPL builds are POSIX-oriented and use GNU Make plus downloaded ncurses/readline; ncurses installs its terminfo database under /usr/local/share/terminfo.

### 🧠 Lessons Learned & Avoided Traps
- Relabeling the old hard Cancel action is not sufficient: Resume requires retained evaluation state and a worker acknowledgement before opening the Paused dialog.
- Manual Pause/Resume must preserve the remaining semantic step-limit count. Only Resume after an actual step-limit stop resets that window.
- A time-budget yield followed by `has_next_redex()` and `print_to()` defeats cooperative pause latency on expanding expressions.
- Do not probe sed or install with `--version`: the macOS/BSD implementations reject that option. Locate them directly and let the dependency configure scripts exercise them.
- Emscripten must be verified only in its separate toolchain build; requiring it during a normal native or header-only configuration would break valid consumers.

### 🏆 Completed Feature Milestones
- 2026-08-11: Added the Studio Pause toolbar action and shared Paused dialog with Cancel and focused Resume buttons.
- 2026-08-11: Added cooperative ordinary-evaluation slices and resumable ordinary, automatic Single Step, and Key Step flows.
- 2026-08-11: Added documented stop-and-restart-on-resume behavior for synchronous Find searches.
- 2026-08-11: Added Pause, step-limit, and worker-protocol regressions; all 24 project tests and 36 focused tests pass.
- 2026-08-11: Synchronized product version 2.6.6 and published Sites version 259.
- 2026-08-11: Added the root INSTALL guide and target-scoped CMake checks for language standards and every native, test and browser build tool.
