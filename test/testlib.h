/* test/testlib.h — the subset of Cosmopolitan's testlib interface used here.
 *
 * jart/cosmopolitan's libc/testlib (TEST(), EXPECT_EQ(want, got), ...) is part
 * of the monorepo build and is not shipped in the cosmocc toolchain, so this
 * header keeps the same spelling and argument order (want before got) so the
 * tests read like, and can move into, cosmopolitan-style test/ trees.
 *
 * One test binary = one translation unit: include this header once, write
 * TEST(suite, name) { ... } blocks, and end the file with TESTLIB_MAIN().
 */
#ifndef COSMO_BDE_TESTLIB_H_
#define COSMO_BDE_TESTLIB_H_

#include <stdio.h>
#include <string.h>

#ifdef __COSMOPOLITAN__
#include <cosmo.h>
#endif

#define TESTLIB_MAX_CASES 256

struct TestlibCase {
    const char *suite;
    const char *name;
    void (*fn)(void);
};

static struct TestlibCase g_testlib_cases[TESTLIB_MAX_CASES];
static int g_testlib_ncases;
static int g_testlib_failed;  /* set by a failing EXPECT in the current case */

#define TEST(SUITE, NAME)                                                   \
    static void SUITE##_##NAME(void);                                       \
    __attribute__((__constructor__)) static void                            \
    SUITE##_##NAME##_register(void) {                                       \
        if (g_testlib_ncases < TESTLIB_MAX_CASES) {                         \
            g_testlib_cases[g_testlib_ncases].suite = #SUITE;               \
            g_testlib_cases[g_testlib_ncases].name = #NAME;                 \
            g_testlib_cases[g_testlib_ncases].fn = SUITE##_##NAME;          \
            g_testlib_ncases++;                                             \
        }                                                                   \
    }                                                                       \
    static void SUITE##_##NAME(void)

#define TESTLIB_FAIL_(...)                                                  \
    do {                                                                    \
        fprintf(stderr, "%s:%d: ", __FILE__, __LINE__);                     \
        fprintf(stderr, __VA_ARGS__);                                       \
        fputc('\n', stderr);                                                \
        g_testlib_failed = 1;                                               \
    } while (0)

#define EXPECT_TRUE(X)                                                      \
    do { if (!(X)) TESTLIB_FAIL_("EXPECT_TRUE(%s) failed", #X); } while (0)

#define EXPECT_FALSE(X)                                                     \
    do { if (X) TESTLIB_FAIL_("EXPECT_FALSE(%s) failed", #X); } while (0)

#define EXPECT_EQ(WANT, GOT)                                                \
    do {                                                                    \
        long long want_ = (long long)(WANT), got_ = (long long)(GOT);       \
        if (want_ != got_)                                                  \
            TESTLIB_FAIL_("EXPECT_EQ(%s, %s): want %lld got %lld",          \
                          #WANT, #GOT, want_, got_);                        \
    } while (0)

#define EXPECT_NE(UNWANT, GOT)                                              \
    do {                                                                    \
        long long unwant_ = (long long)(UNWANT), got_ = (long long)(GOT);   \
        if (unwant_ == got_)                                                \
            TESTLIB_FAIL_("EXPECT_NE(%s, %s): both %lld",                   \
                          #UNWANT, #GOT, got_);                             \
    } while (0)

#define EXPECT_STREQ(WANT, GOT)                                             \
    do {                                                                    \
        const char *want_ = (WANT), *got_ = (GOT);                          \
        if (!want_ || !got_ || strcmp(want_, got_) != 0)                    \
            TESTLIB_FAIL_("EXPECT_STREQ(%s, %s): want \"%s\" got \"%s\"",   \
                          #WANT, #GOT, want_ ? want_ : "(null)",            \
                          got_ ? got_ : "(null)");                          \
    } while (0)

/* ASSERT_* stop the current case on failure (EXPECT_* continue). */
#define ASSERT_TRUE(X)                                                      \
    do { EXPECT_TRUE(X); if (g_testlib_failed) return; } while (0)
#define ASSERT_EQ(WANT, GOT)                                                \
    do { EXPECT_EQ(WANT, GOT); if (g_testlib_failed) return; } while (0)

static int testlib_run_all(void) {
    int failures = 0;
    for (int i = 0; i < g_testlib_ncases; i++) {
        g_testlib_failed = 0;
        g_testlib_cases[i].fn();
        printf("%s %s.%s\n", g_testlib_failed ? "FAIL" : "ok  ",
               g_testlib_cases[i].suite, g_testlib_cases[i].name);
        failures += g_testlib_failed;
    }
    printf("%d test(s), %d failed\n", g_testlib_ncases, failures);
    return failures != 0 || g_testlib_ncases == 0;
}

#ifdef __COSMOPOLITAN__
#define TESTLIB_INIT_() ShowCrashReports()
#else
#define TESTLIB_INIT_() ((void)0)
#endif

#define TESTLIB_MAIN()                                                      \
    int main(void) {                                                        \
        TESTLIB_INIT_();                                                    \
        return testlib_run_all();                                           \
    }

#endif /* COSMO_BDE_TESTLIB_H_ */
