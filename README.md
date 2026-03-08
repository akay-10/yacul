# Yet Another CPP Utility Library (YACUL)

A C++20 utility library providing various components for system programming, concurrency, memory management, and more.

## Modules

### Basic

- Core utilities and macros (DISALLOW_COPY_AND_ASSIGN, etc.)

### Concurrency

- ThreadPool - Thread pool with priority support
- LockRange - Range-based locking primitives
- LockId - Lock ID utilities
- LocklessQueue - Lock-free queue (cerdit: [github:cameron314/concurrentqueue](https://github.com/cameron314/concurrentqueue))

### Hashing

- MurmurHash3 (credit: [github:aappleby/smhasher](https://github.com/aappleby/smhasher))
- SHA hashing
- Bloom Filters

### Logging

- Logger - Structured logging with severity levels
- BinaryLogger - Binary log output support

### Memory

- Buffer - Memory buffer utilities
- Arena - Memory arena allocation
- Allocator - Custom allocator support

### System

- SystemInfo - System information gathering (CPU, memory, disk, network, etc.)
- PopenWrapper - Process execution wrapper
- IoUring - Linux io_uring support (WIP)
- EpollDriver - Linux epoll support (WIP)

### Compression (WIP - Not yet integrated)

- CRC32
- Adler32

### Event (WIP - Not yet integrated)

- EventDriver

### Misc (WIP - Not yet integrated)

- Cache
- QoS
- ScopedExec
- Ranges

### WAL (WIP - Not yet integrated)

- Write-Ahead Log

## Building and Running Tests

```bash
# Build in debug mode
./build.sh build

# Run tests
./build.sh test

# Run all tests with verbose output
./run_tests.sh -v

# Run specific test
./run_tests.sh concurrency threadpool_test -- --gtest_filter="*BasicTasks*"

# List all available tests
./run_tests.sh -l

# Show build configuration
./build.sh info
```

## External Dependencies

- Abseil ([github:abseil/abseil-cpp](https://github.com/abseil/abseil-cpp))
- Google Test ([github:google/googletest](https://github.com/google/googletest))
- Backward-cpp ([github:bombela/backward-cpp](https://github.com/bombela/backward-cpp)), for stack traces in logging library

## Note

- Files starting with an 'os\_' prefix denotes that file/code is an open source
  property.
