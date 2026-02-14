# Yet Another CPP Utility Library (YACUL)

## Hashing

- Murmur hashing 3 (credit: [git:aappleby/smhasher](https://github.com/aappleby/smhasher))
- Bloom Filters

## Building and Running Tests

```bash
# Build in debug mode and run all tests
./build.sh build -d --run-tests

# Build with sanitizers and run tests
./build.sh test --sanitize address -v

# Run all tests with verbose output
./run_tests.sh -v

# Run tests matching "unit" pattern
./run_tests.sh --filter "unit"

# Run specific test
./run_tests.sh myproject test_basics

# List all available tests
./run_tests.sh -l

# Run tests with 30s timeout, continue on failure
./run_tests.sh -c -t 30

# Show build configuration
./build.sh info
```

## External Dependencies

- Abseil
- Google Test

### Note

- Files starting with a 'os\_' prefix denotes that file/code is an open source
  property.
