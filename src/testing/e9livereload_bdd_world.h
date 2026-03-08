#ifndef E9LIVERELOAD_BDD_WORLD_H
#define E9LIVERELOAD_BDD_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "e9ape.h"
#include "e9livereload.h"

#define E9LR_BDD_MAX_EVENTS 64
#define E9LR_BDD_MAX_PATCH_BYTES 64

typedef struct {
    E9LiveReloadEventType type;
    char file_path[256];
    uint32_t patch_id;
    char function_name[128];
    uint64_t patch_address;
    size_t patch_size;
    int error_code;
    char error_msg[256];
} E9LiveReloadEventRecord;

typedef struct {
    char repo_root[512];
    char runner_path[512];
    char original_path[4096];
    char original_tmpdir[512];

    int scenario_serial;

    char workspace_dir[512];
    char workspace_dir_sh[512];
    char source_dir[512];
    char source_dir_sh[512];
    char cache_dir[512];
    char cache_dir_sh[512];
    char bin_dir[512];
    char bin_dir_sh[512];
    char tmp_dir[512];
    char tmp_dir_sh[512];

    char target_fixture_path[512];
    char target_fixture_path_sh[512];
    char target_path[512];
    char target_path_sh[512];
    char missing_target_path[512];
    char missing_target_path_sh[512];

    char compiler_script_path[512];
    char compiler_script_path_sh[512];
    char compiler_invocation_log[512];
    char compiler_invocation_log_sh[512];
    char compiler_output_log[512];
    char compiler_output_log_sh[512];
    char which_script_path[512];
    char which_script_path_sh[512];

    char source_path[512];
    char source_path_sh[512];
    char last_status[256];
    char last_error[512];
    char self_path[512];

    E9LiveReloadConfig config;
    E9LiveReloadEventRecord events[E9LR_BDD_MAX_EVENTS];
    size_t event_count;

    int last_init_result;
    int last_reload_result;
    int last_watch_result;
    int last_poll_result;
    bool last_compiler_available;
    uint32_t last_patch_id;
    int last_revert_result;
    E9LiveReloadStats last_stats;

    E9PatchTargetType patch_target_type;
    uint64_t manual_patch_address;
    size_t manual_patch_size;
    off_t resolved_file_offset;
    bool patch_succeeded;
    bool zipos_warning_seen;
    bool execution_continues;
    uint8_t patch_bytes[E9LR_BDD_MAX_PATCH_BYTES];
    size_t patch_bytes_size;
    uint8_t saved_original_bytes[E9LR_BDD_MAX_PATCH_BYTES];
    size_t saved_original_size;

    uint8_t *ape_buffer;
    size_t ape_buffer_size;
    E9_APEInfo ape_fixture;

    int diff_result;
    int diff_patch_count;
    char diff_function_name[128];
    uint64_t diff_patch_address;
    uint8_t diff_old_bytes[E9LR_BDD_MAX_PATCH_BYTES];
    uint8_t diff_new_bytes[E9LR_BDD_MAX_PATCH_BYTES];
    size_t diff_patch_size;
} E9LiveReloadBddWorld;

int e9lr_world_init(E9LiveReloadBddWorld *world);
void e9lr_world_destroy(E9LiveReloadBddWorld *world);
int e9lr_world_reset_scenario(E9LiveReloadBddWorld *world);
void e9lr_world_attach_callback(E9LiveReloadBddWorld *world);
int e9lr_world_write_source(E9LiveReloadBddWorld *world, const char *relative_path, const char *contents);
int e9lr_world_append_source(E9LiveReloadBddWorld *world, const char *relative_path, const char *suffix);
int e9lr_world_read_text(const char *path, char *buf, size_t buf_size);
bool e9lr_world_file_exists(const char *path);
size_t e9lr_world_count_events(const E9LiveReloadBddWorld *world, E9LiveReloadEventType type);
const E9LiveReloadEventRecord *e9lr_world_find_event(const E9LiveReloadBddWorld *world,
                                                     E9LiveReloadEventType type,
                                                     size_t occurrence);
void e9lr_world_capture_error(E9LiveReloadBddWorld *world);
int e9lr_world_diff_cached_objects(E9LiveReloadBddWorld *world,
                                   const char *old_name,
                                   const char *new_name);
int e9lr_world_prepare_ape_fixture(E9LiveReloadBddWorld *world,
                                   off_t text_offset,
                                   uint32_t text_rva,
                                   size_t text_size,
                                   off_t rdata_offset,
                                   uint32_t rdata_rva,
                                   size_t rdata_size,
                                   off_t zipos_start);
int e9lr_world_apply_manual_patch(E9LiveReloadBddWorld *world);
int e9lr_world_remove_compiler(E9LiveReloadBddWorld *world);
int e9lr_world_restore_compiler(E9LiveReloadBddWorld *world);
int e9lr_world_read_bytes(const char *path, off_t offset, uint8_t *buf, size_t size);

void e9lr_test_stubs_reset(void);
void e9lr_test_stubs_configure_binaryen_patch(const char *function_name,
                                              uint64_t address,
                                              const uint8_t *old_bytes,
                                              const uint8_t *new_bytes,
                                              size_t size);
void e9lr_test_stubs_configure_binaryen_no_changes(bool no_changes);
size_t e9lr_test_stubs_get_flush_count(void);
void *e9lr_test_stubs_get_last_flush_addr(void);
size_t e9lr_test_stubs_get_last_flush_size(void);

#endif
