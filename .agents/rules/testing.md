# Testing Standards & Verification Gates for Agents

This document defines the rules for writing tests, structuring test cases, and validating changes in the OatppBackend repository.

---

## 1. Test Framework & Philosophy

- **Zero-Dependency Harness:** Tests use the lightweight, self-contained harness in `tests/support/TestRunner.hpp`. Do not add GTest, Catch2, or external test runners.
- **Key Macros:**
  - `TEST_CASE("suite: name") { ... }`
  - `EXPECT_TRUE(condition)`
  - `EXPECT_FALSE(condition)`
  - `EXPECT_EQ(actual, expected)`
  - `EXPECT_THROW_AS(statement, ExceptionType)`

---

## 2. AAA Pattern (Arrange, Act, Assert, Cleanup)

Every test case body must be visually structured with blank lines delineating its phases:

```cpp
TEST_CASE("auth: password verification rejects wrong password")
{
    // Arrange
    const std::string plain = "SecurePass123";
    const std::string hashed = auth::passwordHashHex(plain);

    // Act
    const bool valid = auth::verifyPassword("WrongPass", hashed);

    // Assert
    EXPECT_FALSE(valid);
}
```

- When testing exceptions, separate the setup from the `EXPECT_THROW_AS` line with a blank line.
- Always clean up temporary disk files with `std::remove(path.c_str())` or `std::filesystem::remove_all()` at the end of the test.

---

## 3. Hermetic Testing & Port Management

- **No Shared Port Conflicts:** Integration tests must use designated test port ranges:
  - HTTP test server: `18081` to `18090` (looping to find an open port).
  - WebSocket test server: `18281` to `18299`.
- **No Disk Pollution:** Tests that test file logging must use `LoggerGuard` to save and restore process-wide logger state, pointing logs to temporary paths under `/tmp/opencode/` and deleting them in cleanup.
- **In-Memory Databases:** Use `tests::createTestDatabase()` to create isolated `:memory:` SQLCipher databases or dynamically provisioned PostgreSQL databases.

---

## 4. Verification Gate

Before completing any task:
1. Format all modified files:
   ```bash
   /home/arad/.local/bin/clang-format -i <modified_files>
   ```
2. Build and run the entire test suite:
   ```bash
   ctest --test-dir build/debug --output-on-failure
   ```
3. All 14 CTest suites must pass (100% pass rate).
