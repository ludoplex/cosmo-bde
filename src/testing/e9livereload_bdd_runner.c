#include <stdio.h>

#include "e9ape.h"
#include "e9livereload_bdd.h"
#include "e9livereload_bdd_world.h"

int main(void)
{
    E9LiveReloadBddWorld world;
    E9LIVERELOAD_stats_t stats;

    if (e9lr_world_init(&world) != 0) {
        fprintf(stderr, "e9livereload_bdd: failed to initialize test world\n");
        return 1;
    }

    E9LIVERELOAD_run_all(&world, &stats);
    E9LIVERELOAD_print_stats(&stats);

    e9lr_world_destroy(&world);
    return (stats.failed_scenarios == 0 && stats.failed_steps == 0) ? 0 : 1;
}
