add_executable(threadpool_test threadpool_test.cc)

target_include_directories(threadpool_test PRIVATE ${YACUL_INCLUDE_ROOT})

target_link_libraries(threadpool_test libconcurrency GTest::gtest_main)

add_executable(lock_id_test lock_id_test.cc)

target_include_directories(lock_id_test PRIVATE ${YACUL_INCLUDE_ROOT})

target_link_libraries(lock_id_test libconcurrency GTest::gtest_main)

add_executable(lock_range_test lock_range_test.cc)

target_include_directories(lock_range_test PRIVATE ${YACUL_INCLUDE_ROOT})

target_link_libraries(lock_range_test libconcurrency GTest::gtest_main)

add_executable(os_lockless_queue_test os_lockless_queue_test.cc)

target_include_directories(os_lockless_queue_test PRIVATE ${YACUL_INCLUDE_ROOT})

target_link_libraries(os_lockless_queue_test GTest::gtest_main)

add_executable(spinlock_test spinlock_test.cc)

target_include_directories(spinlock_test PRIVATE ${YACUL_INCLUDE_ROOT})

target_link_libraries(spinlock_test libconcurrency GTest::gtest_main)
