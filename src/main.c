/* ═══════════════════════════════════════════════════════════════════════
 * cosmo-bde — BDE with Models
 * Template main.c
 * ═══════════════════════════════════════════════════════════════════════
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "example_types.h"
#include "platform.h"

#ifdef __COSMOPOLITAN__
#include <cosmo.h>
#endif

/* Drop to the least privilege the rest of main() needs. Under cosmocc,
 * pledge() is enforced on Linux and OpenBSD and returns 0 elsewhere;
 * ENOSYS (kernel without seccomp) is tolerated. Native builds: no-op. */
static int drop_privileges(void) {
#ifdef __COSMOPOLITAN__
    if (pledge("stdio", NULL) == -1 && errno != ENOSYS) {
        perror("pledge");
        return -1;
    }
#endif
    return 0;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

#ifdef __COSMOPOLITAN__
    ShowCrashReports();  /* symbolized backtrace on SIGSEGV etc. */
#endif

    printf("cosmo-bde — BDE with Models\n");
    printf("Behavior Driven Engineering with Models\n\n");
    plat_print_info();   /* uses uname(2): run before pledge("stdio") */
    printf("\n");

    if (drop_privileges() != 0) return 1;

    /* Use generated type */
    Example ex;
    Example_init(&ex);
    ex.id = 42;
    snprintf(ex.name, sizeof(ex.name), "Hello from specs!");
    ex.value = 100;
    ex.enabled = 1;

    printf("Example struct:\n");
    printf("  id:      %" PRIu64 "\n", ex.id);
    printf("  name:    %s\n", ex.name);
    printf("  value:   %" PRId32 "\n", ex.value);
    printf("  enabled: %" PRId32 "\n", ex.enabled);
    printf("\n");

    if (Example_validate(&ex)) {
        printf("Validation: PASSED\n");
        return 0;
    }
    printf("Validation: FAILED\n");
    return 1;
}
