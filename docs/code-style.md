# Code Style

The enforceable subset is `docs/coding-standards.md` (4-space indent,
100-column soft limit, naming, include order, ownership, logging rules).
This document records the researched sources, the selected rules, the
formatter configuration, and the validation commands. It matches the
actual repository configuration and the formatted source tree.

## 1. Authoritative sources researched

Actual source material was accessed (not snippets):

- C++ Core Guidelines (`isocpp.github.io/CppCoreGuidelines`): RAII,
  const-correctness, `enum class`, smart-pointer ownership, `noexcept`
  for predicates/getters, static type safety, resource safety. Used for
  language conventions (section 5), not for formatting.
- Google C++ Style Guide (`google.github.io/styleguide/cppguide`):
  2-space indent, attached braces, include order (related header, C,
  C++, other libraries, project), `CamelCase` types, `snake_case`
  variables, no `using namespace` in headers, small inline functions
  only in headers. Base for `.clang-format` (`BasedOnStyle: Google`);
  indentation deliberately widened to 4 spaces (see section 12).
- LLVM Coding Standards (`llvm.org/docs/CodingStandards`): 80-column
  discussion, file-header/`\file` Doxygen, `#include` minimal list,
  `class` vs `struct` use, comment-as-prose, `clang-format` automation.
  Adopted: Doxygen `///` for public APIs, comment-as-prose, automation
  over manual policing. Rejected: 80 columns (too tight for oatpp type
  names; 100 chosen deliberately) and LLVM header guards (repo uses
  `#pragma once` per existing convention).
- ClangFormat style options (`clang.llvm.org/docs/ClangFormatStyleOptions`):
  `BasedOnStyle`, `IndentWidth`, `ColumnLimit`, `PointerAlignment`,
  `ReferenceAlignment`, `BreakBeforeBraces`, `SortIncludes`,
  `IncludeBlocks`, `NamespaceIndentation`, `FixNamespaceComments`.
  All keys used in `.clang-format` were verified against
  `clang-format --dump-config`.
- CMake language manual (`cmake.org/cmake/help/latest/manual/cmake-language.7`):
  command invocations, bracket/quoted/unquoted arguments, control
  structures, variables, lists. Used for CMake style (lowercase
  commands, uppercase keywords, 4-space indent).
- cmake-format configuration (`cmake-format.readthedocs.io`):
  `format.line_width`, `tab_size`, `command_case`, `keyword_case`,
  YAML nesting (`format:`/`lint:` top-level keys). Verified with
  `cmake-format --dump-config yaml`.
- EditorConfig (`editorconfig.org`): `root`, `charset`, `end_of_line`,
  `indent_style`, `indent_size`, `trim_trailing_whitespace`,
  `insert_final_newline`. Used verbatim in `.editorconfig`.

`websearch` returned no results in this environment, so primary-source
`webfetch` was used instead. No blog summaries were relied upon.

Selected set is coherent: Google formatting + Core-Guidelines language
rules + LLVM documentation discipline + CMake official syntax. Where
Google and LLVM conflict (column limit, header guards), one was chosen
deliberately and applied consistently (see section 12).

## 2. C and C++ conventions

- Standard: C++17 enforced (`CMAKE_CXX_STANDARD 17`, `REQUIRED`, extensions
  `OFF`). No C++20 (`std::jthread` forbidden). Verified in
  `CMakeLists.txt:10-12` and `.vscode/settings.json`.
- Const-correctness, move semantics for settings/handlers, deleted copies
  for RAII owners, `noexcept` for predicates and simple getters.
- Explicit ownership: constructor injection with `shared_ptr`,
  null-checked. `std::thread` only in `ManagedThread`. No `detach`, no raw
  owning `new`, no singletons except `Logger::instance()`.
- `enum class` with `toString`/parse helpers (`UserRole`). No magic numbers.
- No `using namespace` in headers. `using` only in `.cpp` or function scope.
- This task is style/readability only. No ownership, lifetime, exception,
  overload, template, or ABI changes were made.

## 3. Naming rules

Preserved from `docs/coding-standards.md` (no public renames):

- Files: `snake_case` (`AppSettings.hpp` types inside, file itself
  `AppSettings.hpp`? Actually files are `CamelCase` matching the primary
  type: `JwtService.hpp`. Historical note: `coding-standards.md` says
  `snake_case` files, but the tree uses `CamelCase` files matching types
  (`AuthController.hpp`). The enforced rule is: file name matches the
  primary type it defines; new files follow the existing `CamelCase` file
  convention. Documented here as an intentional clarification, not a rename.
- Types: `CamelCase` (`JwtService`, `AppSettings`, `TokenClaims`).
- Methods: `lowerCamel` (`create`, `verify`, `extractBearer`).
- Variables/parameters: `snake_case` (`database_path`, `jwt_secret` locals as
  `databasePath`? Actually members are `camelCase` with trailing `_` for
  private: `secret_`, `issuer_`; locals/params are `camelCase` or
  `snake_case` consistently per file. The rule is: private members end with
  `_`, locals use `camelCase`, no misleading abbreviations.
- Namespaces: `app::<layer>` (`logging`, `errors`, `config`, `database`,
  `repositories`, `auth`, `services`, `validation`, `serialization`,
  `domain`, `dto`, `controllers`, `middlewares`, `websocket`,
  `infrastructure`, `application`).
- Constants/compile defines: `APP_*` (`APP_WITH_ZLIB`, `APP_CONFIG_JSON`).
- CMake options/targets: `APP_*` options, `app_core`/`app_server`/`app_tests`
  targets, `apply_app_*` functions.
- Macros: `UPPER_SNAKE`, used only for feature guards and oatpp codegen.
- No renames of public symbols, exported functions, config keys, protocol
  fields, or database identifiers were made.

## 4. Indentation, whitespace, blank lines, braces

- Indent: 4 spaces, no tabs (`IndentWidth: 4`, `ContinuationIndentWidth: 4`,
  `UseTab: Never`). Verified: no tab characters in `src/`, `include/`,
  `cmake/`, `tests/`.
- Width: 100-column soft limit (`ColumnLimit: 100`). `clang-format` wraps
  longer lines (e.g. `src/main.cpp:45`, `AuthController.cpp:14` now wrapped).
  Remaining >100 lines are only `#error` directives and string literals that
  cannot be split safely.
- Spaces: around operators, after commas, around control-flow keywords
  (`if (x)`), never inside empty parentheses. Enforced by `clang-format`.
- Pointers/references: `const char*`, `const std::string&` (star/amp attached
  to the type). In `clang-format` terminology this is `PointerAlignment: Left`,
  `ReferenceAlignment: Left` (verified: `Left` produces `int* a`).
- Trailing whitespace: forbidden (`trim_trailing_whitespace = true`). Verified:
  no trailing whitespace in project-owned files (only `external/` binary
  matches).
- Blank lines: separate logical units, not every statement. One blank line
  between declarations/sections, around `#if` blocks where they delimit
  backends/parsers. No mechanical double blank lines. Checked: no file-scope
  function immediately after `#endif` without separation; in-function `#endif`
  blocks (e.g. `ConfigFactory.cpp`, `AppSettings.cpp`) intentionally have no
  extra blank line because they are mid-function conditionals.
- Braces: Allman (opening brace on its own line for classes, functions,
  namespaces, and control-flow blocks). `BreakBeforeBraces: Allman`,
  `AccessModifierOffset: -4` (access specifiers at column zero, members at
  four spaces). Always use braces for control flow, even single statements.
- EOF: exactly one LF (`insert_final_newline = true`, `end_of_line = lf`).
  Fixed: `docs/archives/imp.md`, `docs/archives/ CodeStyle.md`,
  `.vscode/c_cpp_properties.json`, `.vscode/extensions.json`.
- Encoding: UTF-8, LF only (`.gitattributes` `text=auto eol=lf`). No CRLF in
  project files. No BOM.

## 5. Include ordering and header organization

Order (blank line separates groups, sorted within groups):

1. Own header (`#include "app/auth/JwtService.hpp"` first in
   `src/auth/JwtService.cpp`).
2. C/C++ stdlib (`<csignal>`, `<string>`, `<chrono>`).
3. Third-party (`<jwt-cpp/jwt.h>`, oatpp, jsoncpp, toml11, pugixml, openssl,
   sqlite).
4. Project-local (`"app/errors/AppException.hpp"`, `"app/..."`).

`clang-format` `SortIncludes: CaseSensitive`, `IncludeBlocks: Preserve` keeps
the grouping; it sorts within blocks but never merges blank-line-separated
blocks. Forward-declare oatpp server types in headers where possible; include
full oatpp headers only in `.cpp`. Headers are self-contained with
`#pragma once` (not `#define` guards, per existing convention).

## 6. Preprocessor and header guards

- All headers use `#pragma once` (first line). No `#define` guards were
  introduced; replacing the strategy repository-wide was explicitly rejected
  for compatibility.
- Every conditional-compilation block (`#if`, `#ifdef`, or `#ifndef`) ends
  with a comment identifying the entire block, including nested and short
  blocks. Simple guards use `#endif // APP_WITH_SWAGGER`; negated guards
  retain the negation (`#endif // !APP_WITH_JSON_PARSER`), and compound
  guards retain the full expression (`#endif // _WIN32 || _WIN64`).
- For `#if`/`#elif`/`#else` selections, the closing comment names all
  alternatives, for example
  `#endif // APP_CONFIG format selection (JSON / TOML / XML / YAML)`.
  A simple `#if`/`#else` block keeps its opening condition as the label.
  Match each `#endif` to its opening directive, not the nearest branch.
- `#error` guards for wrong-backend inclusion (`SqlcipherDatabase.hpp:7`,
  `YamlConfigStore.cpp:4` etc.) are intentional and were not reformatted
  beyond line wrapping.
- No spacing churn inside preprocessor directives beyond what `clang-format`
  enforces.

## 7. CMake formatting and naming

- Commands lowercase (`add_library`, `target_link_libraries`,
  `find_package`), keywords uppercase (`PRIVATE`, `PUBLIC`, `CACHE`, `STRING`).
  Enforced by `.cmake-format.yaml` (`command_case: lower`, `keyword_case:
  upper`).
- 4-space indent, 100-column width (`tab_size: 4`, `line_width: 100`,
  `line_ending: unix`). Applied with `cmake-format -i` to `CMakeLists.txt`,
  `cmake/*.cmake`, `tests/CMakeLists.txt`.
- `if/elseif/else/endif`, `foreach`, `while`, `function`, `macro` blocks
  indented one level; one blank line between logical sections.
- Target-based only: `target_*` with scope. No `include_directories` or
  `add_definitions` (see `docs/cmake.md`).
- Minimum version preserved (`cmake_minimum_required(VERSION 3.20)`); no newer
  syntax introduced. Dependency discovery, linkage, definitions, install rules,
  and output names unchanged (verified by build).
- `CMakePresets.json` / `CMakeUserPresets.json`: 4-space JSON, final newline,
  preserved key order (not sorted, to avoid semantic diff).

## 8. Other languages and project files

- Shell (`scripts/*.sh`): `#!/usr/bin/env bash`, `set -euo pipefail` (except
  `test_matrix.sh` which intentionally uses `set -u` only because it handles
  failures manually via PASS/FAIL counters), 4-space indent, LF, executable
  bit preserved (`-rwxrwxr-x`). `bash -n` clean. No `shfmt`/`shellcheck` in
  this environment; style verified manually.
- JSON: 4-space indent, final newline, UTF-8. Verified with `json.load` for
  `CMakePresets.json`, `CMakeUserPresets.json`, `configs/app.json`,
  `.vscode/*.json`. No sorting (key order preserved).
- YAML/TOML/XML: 4-space indent, final newline. Parsed with `PyYAML`,
  `tomllib`, `xml.etree` (`configs/app.yaml`, `app.toml`, `app.xml` valid).
  `configs/app.conf` / `version.conf` (runtime-generated samples) left
  byte-compatible except final newline.
- Markdown: ATX headings, `-` lists, fenced code blocks with language tags,
  no trailing whitespace, single final newline. Checked across `docs/*.md`.
  No `prettier` in this environment; verified with grep/python checks.
- `.vscode/*.json`: 4-space, final newline, `c++17` preserved.
- No Python/JS/TS project files to format.

## 9. Formatter and linter configuration files

- `.clang-format`: `BasedOnStyle: Google`, `IndentWidth: 4`,
  `ContinuationIndentWidth: 4`, `ColumnLimit: 100`, `BreakBeforeBraces: Allman`,
  `AccessModifierOffset: -4`, `AllowShortFunctionsOnASingleLine: None`,
  `AllowShortBlocksOnASingleLine: Never`, `AllowShortLambdasOnASingleLine: None`,
  `PointerAlignment: Left`, `ReferenceAlignment: Left`, `SortIncludes: CaseSensitive`,
  `IncludeBlocks: Preserve`, `NamespaceIndentation: None`,
  `FixNamespaceComments: true`, `Standard: c++17`. Excludes `external/` and
  `build/` by invocation (see section 10), not by config glob.
- `.cmake-format.yaml`: `format.line_width: 100`, `tab_size: 4`,
  `command_case: lower`, `keyword_case: upper`, `line_ending: unix`. Validated
  with `cmake-format --check` (no warnings after fix; initial
  `with section(...)` Python-style nesting was corrected to YAML nesting).
- `.editorconfig`: `root = true`, `charset = utf-8`, `end_of_line = lf`,
  `insert_final_newline = true`, `trim_trailing_whitespace = true`, 4-space
  for cpp/hpp/cmake/json/yaml/xml/sh/md.
- `.gitattributes`: `text=auto eol=lf` for text types, `binary` for
  `*.db`/`*.o`/`*.log`. Respects LF-only policy even though this workspace
  has no git repo (future-proofing).
- No `.clang-tidy`: only cosmetic checks were in scope; no semantic
  modernization was applied, and introducing opinionated `clang-tidy`
  fix-its without a baseline would risk behavior changes. Documented as a
  future recommendation (section 13).
- Reused existing `cmake/compiler.cmake` warnings (`-Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor
  -Wold-style-cast`) instead of creating conflicting lint configs.

## 10. Commands for formatting and validation

```bash
# C++ (excludes external/ and build/ by explicit file list)
find src include tests -name '*.cpp' -o -name '*.hpp' | sort > /tmp/files.txt
clang-format -i $(cat /tmp/files.txt)
clang-format --dry-run --Werror $(cat /tmp/files.txt)  # must print nothing

# CMake
cmake-format -i CMakeLists.txt cmake/*.cmake tests/CMakeLists.txt
cmake-format --check CMakeLists.txt cmake/*.cmake tests/CMakeLists.txt

# Hygiene (LF, final newline, no trailing whitespace, no tabs)
python3 -c "import os; [print(p) for dp,dn,fn in os.walk('src') for f in fn for p in [os.path.join(dp,f)] if open(p,'rb').read()[-1:] != b'\n']"
grep -rn '[[:blank:]]$' --include='*.cpp' --include='*.hpp' --include='*.cmake' --include='*.txt' --include='*.sh' --include='*.md' src include cmake tests scripts docs CMakeLists.txt
grep -rn $'\t' --include='*.cpp' --include='*.hpp' src include tests

# Build + tests (documented build process)
cmake -S . -B build/style-check -DCMAKE_BUILD_TYPE=Release
cmake --build build/style-check --parallel
ctest --test-dir build/style-check --output-on-failure
```

`clang-format` 23.1.3 and `cmake-format` 0.6.13 were installed via
`pip install clang-format cmake-format` into `~/.local/bin` (not on default
`PATH`; invoke with full path or `export PATH="$HOME/.local/bin:$PATH"`).
They are developer tools only, not new project dependencies.

## 11. Excluded directories and file types

- `external/` (243 MB, `libs/`, `ttyd-1.7.7/`): completely untouched. No
  reformat, rename, or bulk-fix command included it. Verified by filesystem
  check (no mtime change under `external/`).
- `build/` (per-preset trees, `build/matrix/`): generated output, ignored by
  `.gitignore`, never formatted.
- `data/app.db`, `logs/*.log`, `*.o`: binary/runtime artifacts, marked
  `binary` in `.gitattributes`, never opened as text.
- Third-party code outside `external/`: none found. All `src/`/`include/`/`tests/`
  files are project-owned.
- `docs/archives/ CodeStyle.md` (leading space in filename): pre-existing
  archive name preserved as-is to avoid breaking references. Only its missing
  final newline was fixed. Renaming it to remove the leading space is a
  separate recommendation.

## 12. Intentional exceptions and justifications

- Four-space indent, not Google 2-space: the project standard is four spaces
  per level (`IndentWidth: 4`, `ContinuationIndentWidth: 4`, `tab_size: 4`,
  `.editorconfig` root default 4). Google's 2-space convention is noted but
  deliberately not followed; `BasedOnStyle: Google` supplies brace,
  spacing, and include-sorting behavior only.
- Column limit 100, not Google 80 or LLVM 80: oatpp type names
  (`oatpp::websocket::ConnectionHandler::SocketInstanceListener`) and
  `DTO_FIELD` macros cannot wrap at 80 without harming readability. 100 is the
  pre-existing soft limit in `docs/coding-standards.md`; chosen deliberately.
- `#pragma once`, not `#define` guards: existing convention across all 33
  headers; replacing it repository-wide would churn every header for no
  behavior benefit. Google/LlVM guard advice noted but not adopted.
- `PointerAlignment: Left` (`int* a`): matches existing code, opposite of
  intuitive English but verified (`Left` produces `char*`). Documented to
  prevent future flip-flopping.
- `test_matrix.sh` uses `set -u` (not `set -euo pipefail`): intentional,
  because the matrix handles configure/build/test failures manually and must
  continue to the next combination. Adding `-e` would abort the matrix.
- File naming: `coding-standards.md` says `snake_case` files, but the C++
  tree uses `CamelCase` files matching types. No files were renamed; the rule
  going forward is "file name matches primary type" (clarified here).
- Long `#error` lines and unbreakable string literals left >100: splitting
  them would require string concatenation churn with no readability gain.

## 13. Known limitations and future recommendations

- No git repo in this workspace (`fatal: not a git repository`), so diff
  validation used filesystem checks (`find -newer`, `grep`, formatter
  `--dry-run`) instead of `git diff`/`git status`. `external/` untouched was
  verified by mtime, not by `git status`.
- `websearch` returned no results; research used direct `webfetch` of primary
  sources instead. Additional credible sources (Markdown/CommonMark spec,
  ShellCheck wiki) were not fetched because formatters for those types are
  unavailable here.
- No `clang-tidy`: semantic checks (modernize, bugprone) were out of scope
  for a cosmetic pass. Recommended future step: enable a minimal
  `readability-*`/`modernize-use-nullptr` set manually, never auto-applied.
- No `shellcheck`/`shfmt`/`prettier` in this environment; shell and markdown
  were verified manually (`bash -n`, grep checks). CI could add them later
  without changing this standard.
- No full `scripts/test_matrix.sh` run (32+ combos, heavy). Only the default
  configure+build+test was run (see Final Report in session output). Matrix
  remains the documented way to prove feature-flag isolation.

## 14. Manual blank-line readability rules (beyond formatters)

`clang-format` (`IncludeBlocks: Preserve`, no blank-line insertion) and
`cmake-format` (preserves but never inserts blanks between blocks) pass on
files that are still visually cramped. The following rules are
**manually reviewed, not automatically enforced**. Each was applied because
the same file or a sibling file already used the clearer form.

- **R1. Include groups are blank-line separated by layer.** Own header,
  then C/C++ stdlib, then third-party/system, then project-local
  (`src/infrastructure/database/SqliteDatabase.cpp:3-7`: `<filesystem>` and `<sqlite3.h>`
  were merged; split to match `UserRepository.cpp`, `ErrorMapper.cpp`).
- **R2. Repeated schema phases use the same blank-line grouping in every
  backend.** `Json/Toml/YamlConfigStore` separate `server`/`database`/`jwt`/
  `logging` with blanks (e.g. `JsonConfigStore.cpp:60/75/86`);
  `XmlConfigStore::load` packed them (`XmlConfigStore.cpp:73/85/96` fixed to
  match). Mid-function `#endif` blocks get a blank after them only when the
  next statement starts a new schema phase, not mechanically.
- **R3. Adjacent namespace-scope functions and template specializations get
  one blank line between declarations.** Six `logTrace..logCritical` helpers
  (`Logger.hpp:102-124`) and ten `boundsFor<T>` specializations
  (`NumericBounds.hpp:27-75`) were stacked with zero separation; the rest of
  both files already separates items. Single-line DTO fields and dispatch
  tables are exempt (packing is idiomatic there).
- **R4. CMake blocks performing different tasks are blank-line separated.**
  Distinct `if/endif` validation groups (`options.cmake:71-85`, `102-116`)
  and distinct `find_package` / `target_link_libraries` groups
  (`dependencies.cmake:35-50`, `84-98`) now use blanks, matching the same
  files' own `ZLIB`/`SWAGGER`/database groups. `cmake-format` preserves these
  blanks; it does not insert them.
- **R5. Long test bodies separate setup/act/assert/cleanup phases.**
  Four-phase tails (`test_config.cpp:218-225`) and guard/setup/readiness
  transitions (`test_websocket.cpp:122-131`) use blanks, matching
  `test_logging.cpp` phase separation. Short tests (3-14 lines) need no
  internal blanks.

Formatter-enforced, do not override manually: trailing-comment column
alignment (`AlignTrailingComments: Kind: Always` — e.g.
`test_lifecycle.cpp:52` 17-space alignment is clang-format output, not a
violation) and CMake comment reflow (cmake-format rewrites
`options.cmake:3-12` into paragraph form; a manual bullet list fails
`cmake-format --check`, so the paragraph is kept and documented as a
formatter limitation).

## 15. Endpoint and registration-section formatting (mandatory)

A file-level conditional-compilation block followed by an independent
declaration or registration section is separated by exactly one blank line.
Related endpoint declarations stay grouped with no blank lines between them
(`clang-format` preserves these blanks; it does not insert them):

```cpp
#if defined(APP_WITH_SWAGGER)
    ENDPOINT_INFO(login);
    ENDPOINT_INFO(whoAmI);
#endif // APP_WITH_SWAGGER

    ENDPOINT("POST", "/auth/login", login, BODY_DTO(Object<dto::LoginRequestDto>, body));
    ENDPOINT("GET", "/auth/who-am-i", whoAmI, REQUEST(std::shared_ptr<IncomingRequest>, request));
```

Applied to `include/app/controllers/AuthController.hpp:35-40` (blank added
after `#endif`, blank removed between the two `ENDPOINT`s),
`include/app/controllers/EchoController.hpp:29-32` (blank added after
`#endif`), and the analogous `DTO_FIELD_INFO` → `DTO_FIELD` transitions in
`include/app/dto/AuthDtos.hpp:13-18,26-33,43-48` (former `ErrorDto.hpp` removed
with the JSON envelope). Mid-function conditionals,
consecutive preprocessor chains, and directives embedded in declarations keep
their context-dependent layout per section 14/R2.

## 16. Allman brace style and class layout (mandatory)

Allman braces are **formatter-enforced** (`BreakBeforeBraces: Allman`,
verified with clang-format 23.1.3). The opening brace goes on its own line
for classes, structs, namespaces, functions, constructors, destructors,
`if`/`else`, `switch`, `for`, `while`, `do`, lambdas with block bodies, and
`try`/`catch`. Braced initializer lists (e.g. `= {`, `toml::table{`) keep
their attached form; they are initializers, not compound statements. Empty
bodies are split (`AllowShortBlocksOnASingleLine: Never`,
`AllowShortFunctionsOnASingleLine: None`,
`AllowShortLambdasOnASingleLine: None`); single-line getters and forwarding
constructors are expanded to the canonical layout. Representative example
(`include/app/controllers/AuthController.hpp`, adapted to the final style):

```cpp
class AuthController : public oatpp::web::server::api::ApiController
{
public:
    AuthController(const std::shared_ptr<ObjectMapper>& objectMapper,
                   std::shared_ptr<services::AuthService> authService,
                   std::shared_ptr<auth::JwtService> jwtService);

#if defined(APP_WITH_SWAGGER)
    ENDPOINT_INFO(login);
    ENDPOINT_INFO(whoAmI);
#endif // APP_WITH_SWAGGER

    ENDPOINT("POST", "/auth/login", login, BODY_DTO(Object<dto::LoginRequestDto>, body));
    ENDPOINT("GET", "/auth/who-am-i", whoAmI, REQUEST(std::shared_ptr<IncomingRequest>, request));

private:
    std::shared_ptr<services::AuthService> authService_;
    std::shared_ptr<auth::JwtService> jwtService_;
};
```

- Class/struct brace placement: opening brace on its own line; closing brace
  with trailing semicolon in conventional form (`};`).
- Access specifiers (`public:`, `protected:`, `private:`) at column zero, no
  indentation (`AccessModifierOffset: -4`); clang-format also emits one blank
  line before each specifier.
- Class members indented exactly four spaces; never tabs.
- Function definitions use the canonical layout (name and `(` together, brace
  on the next line):
```cpp
ReturnType ClassName::functionName(Arguments)
{
    // Function implementation.
}
```
  Declarations ending with `;` are never given artificial bodies.
- Continuation indentation is four spaces (`ContinuationIndentWidth: 4`,
  `BracedInitializerIndentWidth: 4`); multiline parameter lists and
  initializer lists wrap consistently at `ColumnLimit: 100`.
- Preprocessor directives stay unindented (`IndentPPDirectives: None`).
- Manually reviewed (formatter preserves but does not insert): endpoint
  blank-line rule (section 15) and logical phase separation (section 14/R2,
  R5). Do not fight the formatter with manual edits its configuration
  reverses; fix configuration first.

---

*Generated as part of the repository-wide code-style standardization task
(`docs/archives/ CodeStyle.md`). Matches `.clang-format`, `.cmake-format.yaml`,
`.editorconfig`, `.gitattributes` and the formatted tree. Section 14 added by
the visual-readability audit; rules R1-R5 are manually reviewed. Section 15
added with the four-space standard; the endpoint rule is manually reviewed.
Section 16 added with the Allman standard; brace placement, access-specifier
position, and member indentation are formatter-enforced.*
