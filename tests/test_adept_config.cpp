// Adept is benchmarked with the thread-unsafe stack (ADEPT_STACK_THREAD_UNSAFE,
// its documented option for single-threaded use), set for every user of
// adb::adept (cmake/deps.cmake), read from adept::is_thread_unsafe().

#include <adept.h>

#include <gtest/gtest.h>

TEST(AdeptConfig, StackIsThreadUnsafe)
{
    EXPECT_TRUE(adept::is_thread_unsafe()) << "adb::adept does not define ADEPT_STACK_THREAD_UNSAFE";
}
