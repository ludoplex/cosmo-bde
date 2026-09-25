/* cosmo-bde — platform layer
 *
 * Built with cosmocc (the default toolchain) this file compiles once into an
 * Actually Portable Executable, and the host operating system is chosen at
 * run time with Cosmopolitan's IsLinux()/IsXnu()/IsWindows()/... predicates
 * from <cosmo.h> (libc/dce.h). One binary answers correctly on every system
 * it runs on; there is no per-OS build.
 *
 * Built natively (`make CC=cc`) the same functions use only POSIX.1-2008,
 * with uname(2) queried at run time rather than #ifdef __linux__ / __APPLE__
 * chains. The single #ifdef below selects the toolchain, not an OS.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"

#ifdef __COSMOPOLITAN__
#include <cosmo.h>
static const char *cosmo_os_name(void) {
    if (IsLinux()) return "Linux";
    if (IsXnu()) return "macOS";
    if (IsWindows()) return "Windows";
    if (IsFreebsd()) return "FreeBSD";
    if (IsOpenbsd()) return "OpenBSD";
    if (IsNetbsd()) return "NetBSD";
    return NULL;
}
static const int kPlatIsApe = 1;
#else
static const char *cosmo_os_name(void) { return NULL; }
static const int kPlatIsApe = 0;
#endif

/* ── Filesystem (POSIX; Cosmopolitan maps these onto every host) ──── */

int plat_file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

int plat_mkdir(const char *path) {
    return mkdir(path, 0755);
}

int plat_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

/* ── Time (POSIX.1-2008 clock_gettime / nanosleep) ────────────────── */

int64_t plat_time_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void plat_sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0) {
        /* resume after EINTR with the remaining time */
    }
}

/* ── Platform info (run-time detection) ───────────────────────────── */

const char *plat_os_name(void) {
    static char sysname[sizeof(((struct utsname *)0)->sysname)];
    struct utsname u;
    const char *name = cosmo_os_name();
    if (name) return name;
    if (uname(&u) != 0) return "unknown";
    if (strcmp(u.sysname, "Darwin") == 0) return "macOS";
    snprintf(sysname, sizeof(sysname), "%s", u.sysname);
    return sysname;
}

const char *plat_arch_name(void) {
    static char machine[sizeof(((struct utsname *)0)->machine)];
    struct utsname u;
    if (uname(&u) != 0) return "unknown";
    if (strcmp(u.machine, "aarch64") == 0 || strcmp(u.machine, "arm64") == 0)
        return "arm64";
    if (strcmp(u.machine, "x86_64") == 0 || strcmp(u.machine, "amd64") == 0)
        return "x86_64";
    snprintf(machine, sizeof(machine), "%s", u.machine);
    return machine;
}

int plat_is_ape(void) {
    return kPlatIsApe;
}

void plat_print_info(void) {
    printf("Platform: %s\n", plat_os_name());
    printf("Architecture: %s\n", plat_arch_name());
    if (plat_is_ape()) {
        printf("Runtime: Cosmopolitan APE (Actually Portable Executable)\n");
    } else {
        printf("Runtime: native\n");
    }
}
