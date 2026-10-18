# Coding Style and Formatting Rules

These rules are enforced across all C++ source and header files in `include/`, `src/`, and `tests/`.

## Layout & Allman Bracing
- Every class, struct, function, constructor, destructor, control statement (`if`, `else`, `for`, `while`, `do`), `try`/`catch`, and namespace uses Allman bracing (opening brace on its own line).
- Access specifiers (`public:`, `protected:`, `private:`) are placed at column 0 with a blank line before each specifier.
- Complex `case` blocks in `switch` statements must be scoped with `{ ... }` on their own lines.

## Vertical Spacing & Readability
- Put a blank line before any `return` or `throw` statement (unless it is a 1-line guard or function).
- Put a blank line after the closing brace of any `if`, `for`, `while`, or `try`/`catch` block when more code follows.
- Put a blank line between variable declarations/initializations and logic.
- Put a blank line between methods, constructors, destructors, and member variable declarations in class/struct definitions.
- Separate Arrange, Act, Assert, and Cleanup steps in test cases (`TEST_CASE`) with blank lines.
- Put a blank line before and after section-delimiting `#if`, `#ifdef`, `#endif` blocks.

## Comments & Preprocessor
- Always comment the closing brace of namespaces: `}  // namespace app::...`
- Always comment `#endif` with the macro condition: `#endif  // APP_...`
- Prefer `#ifdef FOO` / `#ifndef FOO` over `#if defined(FOO)` for single defines.

## Types & Idioms
- Left alignment for pointer/reference symbols: `const std::string&`, `char*`.
- Prefer early return guard clauses over deep indentation.
- Never use `.c_str()` when constructing `oatpp::String` from `std::string`.
- Mark non-throwing getters and move constructors as `noexcept`.
