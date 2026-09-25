/* test/platform_test.c — unit tests for src/platform/platform.c */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>

#include "platform.h"
#include "testlib.h"

static int is_one_of(const char *s, const char *const *set) {
    for (; *set; set++) {
        if (strcmp(s, *set) == 0) return 1;
    }
    return 0;
}

TEST(plat, os_name_is_a_known_host) {
    static const char *const kKnown[] = {"Linux",   "macOS",   "Windows",
                                         "FreeBSD", "OpenBSD", "NetBSD",
                                         NULL};
    const char *os = plat_os_name();
    ASSERT_TRUE(os != NULL);
    EXPECT_TRUE(is_one_of(os, kKnown));
}

TEST(plat, arch_name_is_normalized) {
    static const char *const kKnown[] = {"x86_64", "arm64", NULL};
    const char *arch = plat_arch_name();
    ASSERT_TRUE(arch != NULL);
    EXPECT_TRUE(is_one_of(arch, kKnown));
}

TEST(plat, is_ape_matches_toolchain) {
#ifdef __COSMOPOLITAN__
    EXPECT_EQ(1, plat_is_ape());
#else
    EXPECT_EQ(0, plat_is_ape());
#endif
}

TEST(plat, monotonic_clock_advances_across_sleep) {
    int64_t t0 = plat_time_ms();
    ASSERT_TRUE(t0 >= 0);
    plat_sleep_ms(20);
    int64_t t1 = plat_time_ms();
    EXPECT_TRUE(t1 - t0 >= 15);
}

TEST(plat, mkdir_then_is_dir_and_exists) {
    char path[64];
    snprintf(path, sizeof(path), "build/platform_test.%ld", (long)getpid());
    EXPECT_FALSE(plat_file_exists(path));
    ASSERT_EQ(0, plat_mkdir(path));
    EXPECT_TRUE(plat_file_exists(path));
    EXPECT_TRUE(plat_is_dir(path));
    EXPECT_EQ(0, rmdir(path));
    EXPECT_FALSE(plat_is_dir(path));
}

TESTLIB_MAIN()
