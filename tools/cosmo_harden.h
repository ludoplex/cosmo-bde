/* tools/cosmo_harden.h — process hardening shared by the Ring 0 generators.
 *
 * Built with cosmocc: ShowCrashReports() gives symbolized backtraces on a
 * crash, and pledge() restricts the process to what a generator needs —
 * stdio plus reading specs (rpath) and writing generated files and output
 * directories (wpath, cpath). pledge() is enforced on Linux and OpenBSD and
 * returns 0 on other hosts; ENOSYS (kernel without seccomp) is tolerated.
 * Built natively (make CC=cc): tool_harden() is a no-op.
 */
#ifndef COSMO_BDE_TOOLS_HARDEN_H_
#define COSMO_BDE_TOOLS_HARDEN_H_

#ifdef __COSMOPOLITAN__
#include <cosmo.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static inline void tool_harden(void) {
    ShowCrashReports();
    if (pledge("stdio rpath wpath cpath", NULL) == -1 && errno != ENOSYS) {
        perror("pledge");
        exit(1);
    }
}
#else
static inline void tool_harden(void) {}
#endif

#endif /* COSMO_BDE_TOOLS_HARDEN_H_ */
