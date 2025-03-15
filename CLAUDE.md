# SuperGenius Development Guide

## Important Guidelines
- NEVER modify files directly - suggest changes but let the user make the actual edits
- Do not commit changes without explicit user permission

## Build Commands
- Build project: `cd build/OSX && mkdir Debug && cd Debug && cmake .. -DCMAKE_BUILD_TYPE=Debug -G "Ninja" && ninja`
- Release build: `cd build/OSX && mkdir Release && cd Release && cmake .. -DCMAKE_BUILD_TYPE=Release -G "Ninja" && ninja`
- Enable testing: Add `-DTESTING=ON` to cmake arguments

## Test Commands
- Run all tests: `ninja test`
- Run specific test: `./test_bin/<test_executable> --gtest_filter=<TestCase.TestName>`
- Example: `./test_bin/buffer_test --gtest_filter=BufferTest.EmptyBuffer`
- Show all tests: `./test_bin/<test_executable> --gtest_list_tests`
- Tests location: `build/OSX/Debug/test_bin/`

## Code Style
- Style: Based on Microsoft with modifications (see .clang-format)
- Indent: 4 spaces
- Line length: 120 characters maximum
- Classes/Methods: PascalCase
- Variables: camelCase
- Constants: ALL_CAPS
- Parentheses: space after opening and before closing: `if ( condition )`
- Braces: Each on their own line
- Error Handling: Use outcome::result<T> pattern for error propagation
- Namespaces: Use nested namespaces with full indentation
- Comments: Document interfaces and public methods

## Testing Practice
- Unit tests should be placed in the test/ directory matching source structure
- Use GTest framework for unit tests
- Test names should be descriptive of what they're testing