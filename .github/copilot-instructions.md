Project: EQEmu (peqsourcecode) — quick guide for automated coding agents

This repository is a large C++ server project built with CMake. The goal of these
instructions is to give an AI coding agent the minimal, concrete facts needed to
be productive without asking trivial follow-ups.

- Big picture
  - Build system: top-level `CMakeLists.txt` drives the build. Common CMake
    options that change behaviour: `EQEMU_BUILD_SERVER`, `EQEMU_BUILD_LOGIN`,
    `EQEMU_BUILD_TESTS`, `EQEMU_PREFER_LUA`, `TLS_LIBRARY_SELECTION`, and
    `EQEMU_BUILD_STATIC`. Inspect `CMakeLists.txt` for dependency detection
    (MySQL/MariaDB, Lua/LuaJIT, OpenSSL/mbedTLS, libsodium, libuv, fmt).
  - Runtime components (each in its own subdirectory / CMake target):
    - `world/` — main game world server (process manager CLI tools live under `world/cli`).
    - `zone/` — zone server with the bulk of gameplay logic (spells, NPCs,
      AI, Lua bindings).
    - `loginserver/` — login/auth service (requires TLS to build).
    - `queryserv/` — database query helper service.
    - `eqlaunch/`, `shared_memory/`, `ucs/` — process launch, IPC, and unicode services.
  - `common/` contains engine core code (networking, serialization, opcode
    tables, shared types). Prefer reading `common/emu_constants.h`,
    `common/emu_opcodes.h`, and `common/database.cpp` to learn core data flows.

- Project patterns & conventions
  - C++20 codebase but keeps compatibility shims for MSVC (see top-level
    `CMakeLists.txt`, uses `/bigobj`, `/MP`, and defines like `NOMINMAX`).
  - Lua/Perl embed: Lua is bound with `luabind` (see `zone/` Lua files such as
    `zone/lua_spell.cpp`) and can be built with LuaJIT or Lua 5.1. There is a
    `EQEMU_SANITIZE_LUA_LIBS` option to restrict the Lua standard libraries.
  - Logging tends to use project-specific helpers (search for `Log*` or
    `LogSpells` in `zone/` for examples). Look at `eqemu_logsys.cpp` in `common/`.
  - Database code is in `common/database*.cpp` and `queryserv/` — the build
    requires either MySQL or MariaDB (CMake enforces this).
  - Many features are controlled by compile-time macros and CMake options, so
    small changes may require a rebuild.

- Integration points and 3rd-party libs
  - Networking: `libuv` (submodule), `websocketpp` (submodule), and project
    wrappers in `common/`.
  - TLS: OpenSSL preferred; mbedTLS is optional — chosen via
    `-DTLS_LIBRARY_SELECTION=OpenSSL|mbedTLS`.
  - Database: MySQL or MariaDB (one is required). `CMakeLists.txt` shows how
    the selection maps to include/link variables.
  - libsodium enables additional security features; enable with CMake detection.
  - RecastNavigation is used for pathfinding (submodule under `submodules/`).

- How to build (examples you can run in PowerShell / Linux)
  - Windows (PowerShell, using Visual Studio generator):
    - mkdir build; cd build
    - cmake -G "Visual Studio 16 2019" -A x64 .. -DEQEMU_BUILD_SERVER=ON -DTLS_LIBRARY_SELECTION=OpenSSL
    - cmake --build . --config RelWithDebInfo --target world
  - Linux/macOS (bash):
    - mkdir build && cd build
    - cmake .. -DEQEMU_BUILD_SERVER=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
    - make -j$(nproc)
  - Common flags to toggle in CMake: `-DEQEMU_BUILD_LUA=ON/OFF`,
    `-DEQEMU_BUILD_PERL=ON/OFF`, `-DEQEMU_BUILD_TESTS=ON/OFF`.

- Debugging & quick triage
  - Binaries are built under the `build/` tree in generator-specific locations
    (Visual Studio solution or makefiles). Run the `world` and `zone` targets
    separately when investigating runtime issues.
  - Search for `LogSpells`, `Crash`, or `eqemu_exception` for common error
    reporting paths. `crash.cpp` in `common/` holds crash-report behavior.

- Small README-like examples to reference code patterns
  - Gameplay logic: `zone/spells.cpp` and `zone/spell_effects.cpp` — show how
    spells, SPAs, and focus effects are implemented and logged (use these as
    examples when modifying spell behavior).
  - Lua bindings: `zone/lua_spell.cpp`, `zone/lua_general.cpp` — use existing
    functions as templates for exposing C++ to Lua via `luabind`.
  - Network opcodes/streams: `common/emu_opcodes.h`, `common/eq_packet*.h` —
    modifying packet formats requires careful coordination between client
    files and server serialization code.

- When editing code, be conservative
  - This project has many cross-component dependencies (database, opcodes,
    Lua bindings). Prefer small, testable changes. If changing ABI or packet
    layout, update both the `common/` headers and any corresponding client
    import tools under `client_files/`.

- Where to look first when asked to implement a change
  - If feature touches gameplay: start in `zone/` and `common/` for shared types.
  - If it touches persistence: inspect `common/database.cpp` and `queryserv/`.
  - If it touches authentication or TLS: inspect `loginserver/` and
    top-level CMake TLS flags.

If anything in these notes is unclear or you need more specific examples (build
commands for a particular Visual Studio version, common runtime arguments, or
where logs are written on disk), tell me which area to expand and I'll update
this file.
