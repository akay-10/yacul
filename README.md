# Yet Another CPP Utility Library (YACUL) (WIP)

## Hashing

- Murmur hashing 3 (credit: [git:aappleby/smhasher](https://github.com/aappleby/smhasher))
- Bloom Filters

## Building and Running Tests

```bash
# Build in debug mode
./build.sh build

# Run tests
./build.sh test

# Run all tests with verbose output
./run_tests.sh -v

# Run specific test
./run_tests.sh concurreny threadpool_test -- --gtest_filter="*BasicTasks*"

# List all available tests
./run_tests.sh -l

# Show build configuration
./build.sh info
```

## External Dependencies

- Abseil
- Google Test

### Note

- Files starting with a 'os\_' prefix denotes that file/code is an open source
  property.
