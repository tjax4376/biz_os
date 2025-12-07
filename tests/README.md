# BIZ_OS Test Suite

## Test Organization

Tests are organized by component:

- `unit/` - Unit tests for individual modules
- `integration/` - Integration tests for layer interactions
- `e2e/` - End-to-end tests for user workflows
- `performance/` - Performance benchmarks
- `security/` - Security tests

## Running Tests

### Rust Tests
```bash
cd ai-runtime/aiservd
cargo test
```

### Kernel Tests
```bash
# Run kernel in VM and test syscalls
# TODO: Implement kernel test framework
```

### Integration Tests
```bash
# TODO: Implement integration test suite
```

## Test Coverage

Target: 80% code coverage for all components.

## Writing Tests

- Write tests alongside code
- Use descriptive test names
- Test both success and failure cases
- Mock external dependencies

