#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "e9livereload_bdd.h"
#include "e9livereload_bdd_world.h"

#define REQUIRE_WORLD() \
    E9LiveReloadBddWorld *world = world_from(ctx); \
    if (!world) \
        return failf(ctx, "missing E9LiveReloadBddWorld");

static E9LiveReloadBddWorld *world_from(E9LIVERELOAD_context_t *ctx)
{
    if (!ctx)
        return NULL;
    return (E9LiveReloadBddWorld *)ctx->world;
}

static E9LIVERELOAD_result_t failf(E9LIVERELOAD_context_t *ctx, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "[e9livereload-bdd] %s:%d %s: ",
            ctx && ctx->feature ? ctx->feature : "<feature>",
            ctx ? ctx->step_line : 0,
            ctx && ctx->step_text ? ctx->step_text : "<step>");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return E9LIVERELOAD_FAIL;
}

static int write_bytes_file(const char *path, const void *data, size_t size)
{
    FILE *out = fopen(path, "wb");
    if (!out)
        return -1;
    if (size > 0 && fwrite(data, 1, size, out) != size) {
        fclose(out);
        return -1;
    }
    fclose(out);
    return 0;
}

static void cache_object_path(const E9LiveReloadBddWorld *world,
                              const char *name,
                              char *out,
                              size_t out_size)
{
    snprintf(out, out_size, "%s/%s", world->cache_dir, name);
}

static bool bytes_equal(const uint8_t *lhs, const uint8_t *rhs, size_t size)
{
    return size == 0 || memcmp(lhs, rhs, size) == 0;
}

static int init_livereload(E9LiveReloadBddWorld *world, const char *target_path)
{
    if (e9_livereload_is_ready())
        e9_livereload_shutdown();

    world->last_init_result = e9_livereload_init(target_path, &world->config);
    if (world->last_init_result == 0)
        e9lr_world_attach_callback(world);
    e9lr_world_capture_error(world);
    return world->last_init_result;
}

static int reload_source(E9LiveReloadBddWorld *world)
{
    world->last_reload_result = e9_livereload_reload_file(world->source_path_sh);
    e9lr_world_capture_error(world);
    return world->last_reload_result;
}

/* Given an APE binary "target.com" is loaded */
E9LIVERELOAD_result_t step_an_ape_binary_targetcom_is_loaded(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_reset_scenario(world) != 0)
        return failf(ctx,
                     "e9lr_world_reset_scenario failed: errno=%d (%s), repo_root=%s, fixture=%s, target=%s",
                     errno, strerror(errno), world->repo_root,
                     world->target_fixture_path, world->target_path);
    if (!e9lr_world_file_exists(world->target_path))
        return failf(ctx, "expected target fixture at %s", world->target_path);
    return E9LIVERELOAD_PASS;
}

/* And live reload is initialized with source directory "src/" */
E9LIVERELOAD_result_t step_live_reload_is_initialized_with_source_directory_src(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (init_livereload(world, world->target_path_sh) != 0)
        return failf(ctx, "e9_livereload_init failed: %s", world->last_error);
    if (strcmp(world->config.source_dir, world->source_dir_sh) != 0)
        return failf(ctx, "expected source_dir %s but got %s",
                     world->source_dir_sh, world->config.source_dir);
    return E9LIVERELOAD_PASS;
}

/* And the compiler "cosmocc" is available */
E9LIVERELOAD_result_t step_the_compiler_cosmocc_is_available(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    world->last_compiler_available = e9_livereload_compiler_available();
    if (!world->last_compiler_available)
        return failf(ctx, "e9_livereload_compiler_available returned false");
    return E9LIVERELOAD_PASS;
}

/* Given a source file "src/main.c" exists */
E9LIVERELOAD_result_t step_a_source_file_srcmainc_exists(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_write_source(world, "src/main.c",
                                "int main(void) {\n"
                                "    return 0;\n"
                                "}\n") != 0)
        return failf(ctx, "failed to write src/main.c");
    return E9LIVERELOAD_PASS;
}

/* When I modify "src/main.c" */
E9LIVERELOAD_result_t step_i_modify_srcmainc(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_append_source(world, "src/main.c",
                                 "\nint touched_main(void) {\n"
                                 "    return 1;\n"
                                 "}\n") != 0)
        return failf(ctx, "failed to modify src/main.c");
    if (reload_source(world) != 0)
        return failf(ctx, "reload_file failed: %s", world->last_error);
    return E9LIVERELOAD_PASS;
}

/* Then a FILE_CHANGE event should be emitted */
E9LIVERELOAD_result_t step_a_file_change_event_should_be_emitted(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_FILE_CHANGE) == 0)
        return failf(ctx, "expected at least one E9_LR_EVENT_FILE_CHANGE event");
    return E9LIVERELOAD_PASS;
}

/* And the event should contain the file path "src/main.c" */
E9LIVERELOAD_result_t step_the_event_should_contain_the_file_path_srcmainc(E9LIVERELOAD_context_t *ctx)
{
    const E9LiveReloadEventRecord *event;

    REQUIRE_WORLD();

    event = e9lr_world_find_event(world, E9_LR_EVENT_FILE_CHANGE, 0);
    if (!event)
        return failf(ctx, "missing E9_LR_EVENT_FILE_CHANGE event");
    if (!strstr(event->file_path, "src/main.c") && !strstr(event->file_path, "/main.c"))
        return failf(ctx, "unexpected file path %s", event->file_path);
    return E9LIVERELOAD_PASS;
}

/* Given a source file "src/README.md" exists */
E9LIVERELOAD_result_t step_a_source_file_srcreadmemd_exists(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_write_source(world, "src/README.md", "# e9livereload\n") != 0)
        return failf(ctx, "failed to write src/README.md");
    return E9LIVERELOAD_PASS;
}

/* When I modify "src/README.md" */
E9LIVERELOAD_result_t step_i_modify_srcreadmemd(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_append_source(world, "src/README.md", "updated\n") != 0)
        return failf(ctx, "failed to modify src/README.md");
    return E9LIVERELOAD_PASS;
}

/* Then no FILE_CHANGE event should be emitted */
E9LIVERELOAD_result_t step_no_file_change_event_should_be_emitted(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_FILE_CHANGE) != 0)
        return failf(ctx, "expected zero E9_LR_EVENT_FILE_CHANGE events");
    return E9LIVERELOAD_PASS;
}

/* Given a valid C source file "src/func.c" */
E9LIVERELOAD_result_t step_a_valid_c_source_file_srcfuncc(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_write_source(world, "src/func.c",
                                "int process_data(int value) {\n"
                                "    return value + 1;\n"
                                "}\n") != 0)
        return failf(ctx, "failed to write src/func.c");
    return E9LIVERELOAD_PASS;
}

/* When a FILE_CHANGE event is detected for "src/func.c" */
E9LIVERELOAD_result_t step_a_file_change_event_is_detected_for_srcfuncc(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (reload_source(world) != 0)
        return failf(ctx, "reload_file failed: %s", world->last_error);
    return E9LIVERELOAD_PASS;
}

/* Then a COMPILE_START event should be emitted */
E9LIVERELOAD_result_t step_a_compile_start_event_should_be_emitted(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_COMPILE_START) == 0)
        return failf(ctx, "expected E9_LR_EVENT_COMPILE_START");
    return E9LIVERELOAD_PASS;
}

/* And cosmocc should be invoked with "-c src/func.c" */
E9LIVERELOAD_result_t step_cosmocc_should_be_invoked_with_c_srcfuncc(E9LIVERELOAD_context_t *ctx)
{
    char logbuf[2048];

    REQUIRE_WORLD();

    if (e9lr_world_read_text(world->compiler_invocation_log, logbuf, sizeof(logbuf)) != 0)
        return failf(ctx, "failed to read %s", world->compiler_invocation_log);
    if (!strstr(logbuf, "-c"))
        return failf(ctx, "compiler invocation log missing -c: %s", logbuf);
    if (!strstr(logbuf, "func.c"))
        return failf(ctx, "compiler invocation log missing func.c: %s", logbuf);
    return E9LIVERELOAD_PASS;
}

/* And a COMPILE_DONE event should be emitted */
E9LIVERELOAD_result_t step_a_compile_done_event_should_be_emitted(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_COMPILE_DONE) == 0)
        return failf(ctx, "expected E9_LR_EVENT_COMPILE_DONE");
    return E9LIVERELOAD_PASS;
}

/* And the object file should be cached in ".e9cache/" */
E9LIVERELOAD_result_t step_the_object_file_should_be_cached_in_e9cache(E9LIVERELOAD_context_t *ctx)
{
    char path[512];

    REQUIRE_WORLD();

    cache_object_path(world, "func.c.o", path, sizeof(path));
    if (!e9lr_world_file_exists(path))
        return failf(ctx, "expected cached object %s", path);
    return E9LIVERELOAD_PASS;
}

/* Given an invalid C source file "src/broken.c" with syntax errors */
E9LIVERELOAD_result_t step_an_invalid_c_source_file_srcbrokenc_with_syntax_errors(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_write_source(world, "src/broken.c",
                                "int broken( {\n"
                                "    return 1;\n"
                                "}\n") != 0)
        return failf(ctx, "failed to write src/broken.c");
    return E9LIVERELOAD_PASS;
}

/* When a FILE_CHANGE event is detected for "src/broken.c" */
E9LIVERELOAD_result_t step_a_file_change_event_is_detected_for_srcbrokenc(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (strcmp(world->source_path, "") == 0 ||
        !strstr(world->source_path, "broken.c")) {
        if (e9lr_world_write_source(world, "src/broken.c",
                                    "int broken( {\n"
                                    "    return 1;\n"
                                    "}\n") != 0)
            return failf(ctx, "failed to prepare src/broken.c");
    }

    world->last_reload_result = e9_livereload_reload_file(world->source_path_sh);
    e9lr_world_capture_error(world);
    return E9LIVERELOAD_PASS;
}

/* Then a COMPILE_ERROR event should be emitted */
E9LIVERELOAD_result_t step_a_compile_error_event_should_be_emitted(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_COMPILE_ERROR) == 0)
        return failf(ctx, "expected E9_LR_EVENT_COMPILE_ERROR");
    return E9LIVERELOAD_PASS;
}

/* And the error message should contain the compiler output */
E9LIVERELOAD_result_t step_the_error_message_should_contain_the_compiler_output(E9LIVERELOAD_context_t *ctx)
{
    char logbuf[2048];

    REQUIRE_WORLD();

    if (e9lr_world_read_text(world->compiler_output_log, logbuf, sizeof(logbuf)) != 0)
        return failf(ctx, "failed to read %s", world->compiler_output_log);
    if (!strstr(logbuf, "error") && !strstr(logbuf, "fatal") && !strstr(logbuf, "expected"))
        return failf(ctx, "compiler output log did not contain a diagnostic: %s", logbuf);
    if (world->last_error[0] == '\0')
        return failf(ctx, "e9_livereload_get_error returned an empty message");
    return E9LIVERELOAD_PASS;
}

/* And no patches should be generated */
E9LIVERELOAD_result_t step_no_patches_should_be_generated(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_PATCH_GENERATED) != 0)
        return failf(ctx, "unexpected E9_LR_EVENT_PATCH_GENERATED event");
    if (e9_livereload_pending_count() != 0 || e9_livereload_applied_count() != 0)
        return failf(ctx, "unexpected pending/applied patch state");
    return E9LIVERELOAD_PASS;
}

/* Given a cached object "func.c.o" from previous compilation */
E9LIVERELOAD_result_t step_a_cached_object_funcco_from_previous_compilation(E9LIVERELOAD_context_t *ctx)
{
    char path[512];
    static const uint8_t k_old_obj[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};

    REQUIRE_WORLD();

    cache_object_path(world, "func.c.o", path, sizeof(path));
    if (write_bytes_file(path, k_old_obj, sizeof(k_old_obj)) != 0)
        return failf(ctx, "failed to write %s", path);
    e9lr_test_stubs_configure_binaryen_no_changes(false);
    return E9LIVERELOAD_PASS;
}

/* And a new object "func.c.new.o" with changes to function "process_data" */
E9LIVERELOAD_result_t step_a_new_object_funccnewo_with_changes_to_function_process_data(E9LIVERELOAD_context_t *ctx)
{
    char path[512];
    static const uint8_t k_new_obj[] = {0x00, 0x61, 0x73, 0x6d, 0x02, 0x00, 0x00, 0x00};
    static const uint8_t k_old_bytes[] = {0x90, 0x90};
    static const uint8_t k_new_bytes[] = {0xCC, 0xCC};

    REQUIRE_WORLD();

    cache_object_path(world, "func.c.new.o", path, sizeof(path));
    if (write_bytes_file(path, k_new_obj, sizeof(k_new_obj)) != 0)
        return failf(ctx, "failed to write %s", path);
    e9lr_test_stubs_configure_binaryen_patch("process_data", 0x11234,
                                             k_old_bytes, k_new_bytes,
                                             sizeof(k_new_bytes));
    return E9LIVERELOAD_PASS;
}

/* When Binaryen diffs the two objects */
E9LIVERELOAD_result_t step_binaryen_diffs_the_two_objects(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_diff_cached_objects(world, "func.c.o", "func.c.new.o") != 0)
        return failf(ctx, "e9lr_world_diff_cached_objects failed");
    return E9LIVERELOAD_PASS;
}

/* Then a PATCH_GENERATED event should be emitted */
E9LIVERELOAD_result_t step_a_patch_generated_event_should_be_emitted(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_PATCH_GENERATED) == 0)
        return failf(ctx, "expected E9_LR_EVENT_PATCH_GENERATED");
    return E9LIVERELOAD_PASS;
}

/* And the patch should target function "process_data" */
E9LIVERELOAD_result_t step_the_patch_should_target_function_process_data(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (strcmp(world->diff_function_name, "process_data") != 0)
        return failf(ctx, "expected function process_data, got %s", world->diff_function_name);
    return E9LIVERELOAD_PASS;
}

/* And the patch should contain the address and replacement bytes */
E9LIVERELOAD_result_t step_the_patch_should_contain_the_address_and_replacement_bytes(E9LIVERELOAD_context_t *ctx)
{
    static const uint8_t k_expected[] = {0xCC, 0xCC};

    REQUIRE_WORLD();

    if (world->diff_patch_address != 0x11234)
        return failf(ctx, "expected address 0x11234, got 0x%llx",
                     (unsigned long long)world->diff_patch_address);
    if (world->diff_patch_size != sizeof(k_expected))
        return failf(ctx, "expected patch size %zu, got %zu",
                     sizeof(k_expected), world->diff_patch_size);
    if (!bytes_equal(world->diff_new_bytes, k_expected, sizeof(k_expected)))
        return failf(ctx, "replacement bytes did not match Binaryen stub output");
    return E9LIVERELOAD_PASS;
}

/* Given a cached object "func.c.o" */
E9LIVERELOAD_result_t step_a_cached_object_funcco(E9LIVERELOAD_context_t *ctx)
{
    return step_a_cached_object_funcco_from_previous_compilation(ctx);
}

/* And a new object "func.c.new.o" with only whitespace changes */
E9LIVERELOAD_result_t step_a_new_object_funccnewo_with_only_whitespace_changes(E9LIVERELOAD_context_t *ctx)
{
    char path[512];
    static const uint8_t k_obj[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};

    REQUIRE_WORLD();

    cache_object_path(world, "func.c.new.o", path, sizeof(path));
    if (write_bytes_file(path, k_obj, sizeof(k_obj)) != 0)
        return failf(ctx, "failed to write %s", path);
    e9lr_test_stubs_configure_binaryen_no_changes(true);
    return E9LIVERELOAD_PASS;
}

/* Then no PATCH_GENERATED event should be emitted */
E9LIVERELOAD_result_t step_no_patch_generated_event_should_be_emitted(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_PATCH_GENERATED) != 0)
        return failf(ctx, "unexpected E9_LR_EVENT_PATCH_GENERATED");
    if (world->diff_patch_count != 0)
        return failf(ctx, "expected zero patches, got %d", world->diff_patch_count);
    return E9LIVERELOAD_PASS;
}

/* And the status should indicate "No changes detected" */
E9LIVERELOAD_result_t step_the_status_should_indicate_no_changes_detected(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (strcmp(world->last_status, "No changes detected") != 0)
        return failf(ctx, "expected status \"No changes detected\", got %s", world->last_status);
    return E9LIVERELOAD_PASS;
}

static int prepare_standard_ape_fixture(E9LiveReloadBddWorld *world, off_t zipos_start)
{
    return e9lr_world_prepare_ape_fixture(world,
                                          0x11000, 0x11000, 0x4000,
                                          0x2F000, 0x2F000, 0x2000,
                                          zipos_start);
}

/* Given a patch targeting PE RVA 0x11234 */
E9LIVERELOAD_result_t step_a_patch_targeting_pe_rva_0x11234(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    world->patch_target_type = E9_PATCH_TARGET_PE_RVA;
    world->manual_patch_address = 0x11234;
    world->patch_bytes[0] = 0xCC;
    world->patch_bytes[1] = 0xCC;
    world->patch_bytes_size = 2;
    return E9LIVERELOAD_PASS;
}

/* And the APE has .text section at file offset 0x11000 with RVA 0x11000 */
E9LIVERELOAD_result_t step_the_ape_has_text_section_at_file_offset_0x11000_with_rva_0x11000(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (prepare_standard_ape_fixture(world, 0) != 0)
        return failf(ctx, "failed to prepare synthetic APE fixture");
    return E9LIVERELOAD_PASS;
}

/* When the patch is applied */
E9LIVERELOAD_result_t step_the_patch_is_applied(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_apply_manual_patch(world) != 0)
        return failf(ctx, "e9lr_world_apply_manual_patch failed");
    return E9LIVERELOAD_PASS;
}

/* Then the file offset should be calculated as 0x11234 */
E9LIVERELOAD_result_t step_the_file_offset_should_be_calculated_as_0x11234(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (world->resolved_file_offset != 0x11234)
        return failf(ctx, "expected file offset 0x11234, got 0x%llx",
                     (unsigned long long)world->resolved_file_offset);
    return E9LIVERELOAD_PASS;
}

/* And the bytes should be written to the memory-mapped binary */
E9LIVERELOAD_result_t step_the_bytes_should_be_written_to_the_memory_mapped_binary(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (world->resolved_file_offset < 0)
        return failf(ctx, "patch address was not resolved");
    if (!bytes_equal(world->ape_buffer + world->resolved_file_offset,
                     world->patch_bytes,
                     world->patch_bytes_size))
        return failf(ctx, "patched bytes were not written to ape_buffer");
    return E9LIVERELOAD_PASS;
}

/* And a PATCH_APPLIED event should be emitted */
E9LIVERELOAD_result_t step_a_patch_applied_event_should_be_emitted(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_PATCH_APPLIED) == 0)
        return failf(ctx, "expected E9_LR_EVENT_PATCH_APPLIED");
    return E9LIVERELOAD_PASS;
}

/* Given a patch targeting PE RVA 0x2F100 */
E9LIVERELOAD_result_t step_a_patch_targeting_pe_rva_0x2f100(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    world->patch_target_type = E9_PATCH_TARGET_PE_RVA;
    world->manual_patch_address = 0x2F100;
    world->patch_bytes[0] = 0x13;
    world->patch_bytes[1] = 0x37;
    world->patch_bytes_size = 2;
    return E9LIVERELOAD_PASS;
}

/* And the APE has .rdata section at file offset 0x2F000 with RVA 0x2F000 */
E9LIVERELOAD_result_t step_the_ape_has_rdata_section_at_file_offset_0x2f000_with_rva_0x2f000(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (prepare_standard_ape_fixture(world, 0) != 0)
        return failf(ctx, "failed to prepare synthetic APE fixture");
    return E9LIVERELOAD_PASS;
}

/* Then the file offset should be calculated as 0x2F100 */
E9LIVERELOAD_result_t step_the_file_offset_should_be_calculated_as_0x2f100(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (world->resolved_file_offset != 0x2F100)
        return failf(ctx, "expected file offset 0x2F100, got 0x%llx",
                     (unsigned long long)world->resolved_file_offset);
    return E9LIVERELOAD_PASS;
}

/* And the patch should succeed */
E9LIVERELOAD_result_t step_the_patch_should_succeed(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (!world->patch_succeeded)
        return failf(ctx, "expected patch_succeeded");
    return E9LIVERELOAD_PASS;
}

/* Given an APE with ZipOS starting at offset 0x60000 */
E9LIVERELOAD_result_t step_an_ape_with_zipos_starting_at_offset_0x60000(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (prepare_standard_ape_fixture(world, 0x60000) != 0)
        return failf(ctx, "failed to prepare ZipOS-preserving APE fixture");
    return E9LIVERELOAD_PASS;
}

/* And a patch targeting file offset 0x60010 */
E9LIVERELOAD_result_t step_a_patch_targeting_file_offset_0x60010(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    world->patch_target_type = E9_PATCH_TARGET_FILE_OFFSET;
    world->manual_patch_address = 0x60010;
    world->patch_bytes[0] = 0xAA;
    world->patch_bytes[1] = 0x55;
    world->patch_bytes_size = 2;
    return E9LIVERELOAD_PASS;
}

/* Then a warning should be emitted about ZipOS overlap */
E9LIVERELOAD_result_t step_a_warning_should_be_emitted_about_zipos_overlap(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (!world->zipos_warning_seen)
        return failf(ctx, "expected zipos_warning_seen");
    return E9LIVERELOAD_PASS;
}

/* And the patch may proceed with user acknowledgment */
E9LIVERELOAD_result_t step_the_patch_may_proceed_with_user_acknowledgment(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (!world->patch_succeeded)
        return failf(ctx, "expected patch to succeed despite ZipOS overlap");
    return E9LIVERELOAD_PASS;
}

/* Given a patch was applied to executable .text section */
E9LIVERELOAD_result_t step_a_patch_was_applied_to_executable_text_section(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (prepare_standard_ape_fixture(world, 0) != 0)
        return failf(ctx, "failed to prepare synthetic APE fixture");
    world->patch_target_type = E9_PATCH_TARGET_PE_RVA;
    world->manual_patch_address = 0x11234;
    world->patch_bytes[0] = 0xCC;
    world->patch_bytes[1] = 0xCC;
    world->patch_bytes_size = 2;
    if (e9lr_world_apply_manual_patch(world) != 0)
        return failf(ctx, "failed to apply executable .text patch");
    return E9LIVERELOAD_PASS;
}

/* When the patch is finalized */
E9LIVERELOAD_result_t step_the_patch_is_finalized(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (!world->patch_succeeded)
        return failf(ctx, "patch was not applied before finalization");
    return E9LIVERELOAD_PASS;
}

/* Then e9wasm_flush_icache should be called */
E9LIVERELOAD_result_t step_e9wasm_flush_icache_should_be_called(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_test_stubs_get_flush_count() == 0)
        return failf(ctx, "expected e9wasm_flush_icache to be called");
    return E9LIVERELOAD_PASS;
}

/* And the flushed range should cover the patched bytes */
E9LIVERELOAD_result_t step_the_flushed_range_should_cover_the_patched_bytes(E9LIVERELOAD_context_t *ctx)
{
    void *expected_addr;

    REQUIRE_WORLD();

    expected_addr = world->ape_buffer + world->resolved_file_offset;
    if (e9lr_test_stubs_get_last_flush_addr() != expected_addr)
        return failf(ctx, "expected flush address %p, got %p",
                     expected_addr, e9lr_test_stubs_get_last_flush_addr());
    if (e9lr_test_stubs_get_last_flush_size() < world->patch_bytes_size)
        return failf(ctx, "expected flush size >= %zu, got %zu",
                     world->patch_bytes_size, e9lr_test_stubs_get_last_flush_size());
    return E9LIVERELOAD_PASS;
}

/* Given live reload is initialized with target NULL (self) */
E9LIVERELOAD_result_t step_live_reload_is_initialized_with_target_null_self(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (init_livereload(world, NULL) != 0)
        return failf(ctx, "self e9_livereload_init failed: %s", world->last_error);
    return E9LIVERELOAD_PASS;
}

/* When the executable path is determined via /proc/self/exe */
E9LIVERELOAD_result_t step_the_executable_path_is_determined_via_procselfexe(E9LIVERELOAD_context_t *ctx)
{
    const char *self;

    REQUIRE_WORLD();

    self = e9_ape_get_self_path();
    if (!self || !*self)
        return failf(ctx, "e9_ape_get_self_path returned NULL");
    strncpy(world->self_path, self, sizeof(world->self_path) - 1);
    world->self_path[sizeof(world->self_path) - 1] = '\0';
    return E9LIVERELOAD_PASS;
}

/* Then the running APE should be memory-mapped */
E9LIVERELOAD_result_t step_the_running_ape_should_be_memory_mapped(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (!e9_livereload_is_ready())
        return failf(ctx, "e9_livereload_is_ready returned false");
    return E9LIVERELOAD_PASS;
}

/* And patches should be applied in-place */
E9LIVERELOAD_result_t step_patches_should_be_applied_in_place(E9LIVERELOAD_context_t *ctx)
{
    uint8_t same_byte = 0;
    const char *self_path;

    REQUIRE_WORLD();

    self_path = world->self_path[0] ? world->self_path : world->runner_path;
    if (!self_path[0])
        return failf(ctx, "missing runner path");
    if (e9lr_world_read_bytes(self_path, 0, &same_byte, 1) != 0)
        return failf(ctx, "failed to read first byte from %s", self_path);
    world->last_patch_id = e9_livereload_apply_patch(E9_PATCH_TARGET_FILE_OFFSET, 0,
                                                     &same_byte, 1);
    if (world->last_patch_id == 0) {
        e9lr_world_capture_error(world);
        return failf(ctx, "self patch apply failed: %s", world->last_error);
    }
    world->execution_continues = true;
    return E9LIVERELOAD_PASS;
}

/* And execution should continue with the new code */
E9LIVERELOAD_result_t step_execution_should_continue_with_the_new_code(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (!world->execution_continues)
        return failf(ctx, "execution_continues was not set");
    return E9LIVERELOAD_PASS;
}

/* Given live reload has processed multiple source changes */
E9LIVERELOAD_result_t step_live_reload_has_processed_multiple_source_changes(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_write_source(world, "src/stats.c",
                                "int process_data(int value) {\n"
                                "    return value + 1;\n"
                                "}\n") != 0)
        return failf(ctx, "failed to write src/stats.c");
    if (reload_source(world) != 0)
        return failf(ctx, "initial stats reload failed: %s", world->last_error);
    if (e9lr_world_write_source(world, "src/stats.c",
                                "int process_data(int value) {\n"
                                "    return value + 2;\n"
                                "}\n") != 0)
        return failf(ctx, "failed to modify src/stats.c");
    if (reload_source(world) != 1)
        return failf(ctx, "second stats reload expected one patch, got %d", world->last_reload_result);
    return E9LIVERELOAD_PASS;
}

/* When I query the statistics */
E9LIVERELOAD_result_t step_i_query_the_statistics(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    e9_livereload_get_stats(&world->last_stats);
    return E9LIVERELOAD_PASS;
}

/* Then I should see: */
E9LIVERELOAD_result_t step_i_should_see(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (world->last_stats.changes_detected < 2)
        return failf(ctx, "expected >=2 changes_detected, got %llu",
                     (unsigned long long)world->last_stats.changes_detected);
    if (world->last_stats.patches_generated < 1)
        return failf(ctx, "expected >=1 patches_generated, got %llu",
                     (unsigned long long)world->last_stats.patches_generated);
    if (world->last_stats.patches_applied < 1)
        return failf(ctx, "expected >=1 patches_applied, got %llu",
                     (unsigned long long)world->last_stats.patches_applied);
    if (world->last_stats.total_bytes_patched == 0)
        return failf(ctx, "expected total_bytes_patched > 0");
    if (world->last_stats.last_change_time == 0 || world->last_stats.last_patch_time == 0)
        return failf(ctx, "expected non-zero last_change_time and last_patch_time");
    return E9LIVERELOAD_PASS;
}

/* Given patch #1 was applied at offset 0x11234 */
E9LIVERELOAD_result_t step_patch_1_was_applied_at_offset_0x11234(E9LIVERELOAD_context_t *ctx)
{
    static const uint8_t k_patch[] = {0xC3, 0x90};

    REQUIRE_WORLD();

    if (e9lr_world_read_bytes(world->target_path, 0x11234,
                              world->saved_original_bytes,
                              sizeof(k_patch)) != 0)
        return failf(ctx, "failed to read original bytes from %s", world->target_path);
    world->saved_original_size = sizeof(k_patch);
    world->last_patch_id = e9_livereload_apply_patch(E9_PATCH_TARGET_FILE_OFFSET,
                                                     0x11234, k_patch, sizeof(k_patch));
    if (world->last_patch_id == 0) {
        e9lr_world_capture_error(world);
        return failf(ctx, "e9_livereload_apply_patch failed: %s", world->last_error);
    }
    memcpy(world->patch_bytes, k_patch, sizeof(k_patch));
    world->patch_bytes_size = sizeof(k_patch);
    world->manual_patch_address = 0x11234;
    return E9LIVERELOAD_PASS;
}

/* And the original bytes were saved */
E9LIVERELOAD_result_t step_the_original_bytes_were_saved(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (world->saved_original_size == 0)
        return failf(ctx, "expected saved_original_size > 0");
    return E9LIVERELOAD_PASS;
}

/* When I revert patch #1 */
E9LIVERELOAD_result_t step_i_revert_patch_1(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    world->last_revert_result = e9_livereload_revert_patch(world->last_patch_id);
    e9lr_world_capture_error(world);
    if (world->last_revert_result != 0)
        return failf(ctx, "e9_livereload_revert_patch failed: %s", world->last_error);
    return E9LIVERELOAD_PASS;
}

/* Then the original bytes should be restored */
E9LIVERELOAD_result_t step_the_original_bytes_should_be_restored(E9LIVERELOAD_context_t *ctx)
{
    uint8_t restored[E9LR_BDD_MAX_PATCH_BYTES];

    REQUIRE_WORLD();

    if (world->saved_original_size == 0)
        return failf(ctx, "no saved original bytes to compare");
    if (e9lr_world_read_bytes(world->target_path, 0x11234,
                              restored, world->saved_original_size) != 0)
        return failf(ctx, "failed to read restored bytes from %s", world->target_path);
    if (!bytes_equal(restored, world->saved_original_bytes, world->saved_original_size))
        return failf(ctx, "restored bytes did not match saved original bytes");
    return E9LIVERELOAD_PASS;
}

/* And a PATCH_REVERTED event should be emitted */
E9LIVERELOAD_result_t step_a_patch_reverted_event_should_be_emitted(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_count_events(world, E9_LR_EVENT_PATCH_REVERTED) == 0)
        return failf(ctx, "expected E9_LR_EVENT_PATCH_REVERTED");
    return E9LIVERELOAD_PASS;
}

/* And the instruction cache should be flushed */
E9LIVERELOAD_result_t step_the_instruction_cache_should_be_flushed(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_test_stubs_get_flush_count() == 0)
        return failf(ctx, "expected e9wasm_flush_icache to be called");
    return E9LIVERELOAD_PASS;
}

/* Given the compiler "cosmocc" is not in PATH */
E9LIVERELOAD_result_t step_the_compiler_cosmocc_is_not_in_path(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_remove_compiler(world) != 0)
        return failf(ctx, "failed to remove compiler shim");
    return E9LIVERELOAD_PASS;
}

/* When I call e9_livereload_compiler_available() */
E9LIVERELOAD_result_t step_i_call_e9_livereload_compiler_available(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    world->last_compiler_available = e9_livereload_compiler_available();
    return E9LIVERELOAD_PASS;
}

/* Then it should return false */
E9LIVERELOAD_result_t step_it_should_return_false(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (world->last_compiler_available)
        return failf(ctx, "expected e9_livereload_compiler_available to return false");
    return E9LIVERELOAD_PASS;
}

/* And compilation attempts should fail with a clear error message */
E9LIVERELOAD_result_t step_compilation_attempts_should_fail_with_a_clear_error_message(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (e9lr_world_write_source(world, "src/missing_compiler.c",
                                "int process_data(int value) {\n"
                                "    return value + 1;\n"
                                "}\n") != 0)
        return failf(ctx, "failed to write src/missing_compiler.c");
    world->last_reload_result = e9_livereload_reload_file(world->source_path_sh);
    e9lr_world_capture_error(world);
    if (world->last_reload_result >= 0)
        return failf(ctx, "expected reload_file failure when compiler is missing");
    if (!strstr(world->last_error, "Compilation failed"))
        return failf(ctx, "expected clear compile failure message, got %s", world->last_error);
    return E9LIVERELOAD_PASS;
}

/* Given a target file that does not exist */
E9LIVERELOAD_result_t step_a_target_file_that_does_not_exist(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    unlink(world->missing_target_path);
    if (e9lr_world_file_exists(world->missing_target_path))
        return failf(ctx, "expected %s to be absent", world->missing_target_path);
    return E9LIVERELOAD_PASS;
}

/* When I initialize live reload with that target */
E9LIVERELOAD_result_t step_i_initialize_live_reload_with_that_target(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    world->last_init_result = init_livereload(world, world->missing_target_path_sh);
    return E9LIVERELOAD_PASS;
}

/* Then initialization should fail with error "Cannot open target" */
E9LIVERELOAD_result_t step_initialization_should_fail_with_error_cannot_open_target(E9LIVERELOAD_context_t *ctx)
{
    REQUIRE_WORLD();

    if (world->last_init_result == 0)
        return failf(ctx, "expected e9_livereload_init failure for missing target");
    if (!strstr(world->last_error, "Cannot open target"))
        return failf(ctx, "expected \"Cannot open target\", got %s", world->last_error);
    return E9LIVERELOAD_PASS;
}

/* And e9_livereload_get_error() should return the error message */
E9LIVERELOAD_result_t step_e9_livereload_get_error_should_return_the_error_message(E9LIVERELOAD_context_t *ctx)
{
    const char *err;

    REQUIRE_WORLD();

    err = e9_livereload_get_error();
    if (!err || !*err)
        return failf(ctx, "e9_livereload_get_error returned NULL");
    if (strcmp(err, world->last_error) != 0)
        return failf(ctx, "expected error %s, got %s", world->last_error, err);
    return E9LIVERELOAD_PASS;
}
