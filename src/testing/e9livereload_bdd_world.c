#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "wasm/e9binaryen.h"
#include "wasm/e9wasm_host.h"

#include "e9livereload_bdd_world.h"

static void e9lr_normalize_path(char *path)
{
    for (; *path; ++path) {
        if (*path == '\\')
            *path = '/';
    }
}

static void e9lr_to_shell_path(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    if (src[0] && src[1] == ':') {
        if (j + 3 < dst_size) {
            dst[j++] = '/';
            dst[j++] = (char)((src[0] >= 'a' && src[0] <= 'z') ? (src[0] - 32) : src[0]);
        }
        src += 2;
    }
    while (*src && j + 1 < dst_size) {
        dst[j++] = (*src == '\\') ? '/' : *src;
        src++;
    }
    dst[j] = '\0';
}

static int e9lr_ensure_dir(const char *path)
{
    char tmp[512];
    size_t len;

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    e9lr_normalize_path(tmp);
    len = strlen(tmp);
    if (len == 0)
        return 0;

    for (size_t i = 0; i < len; ++i) {
        if (tmp[i] != '/')
            continue;
        if (i == 0 ||
            (i == 2 && tmp[1] == ':') ||
            (i == 2 &&
             tmp[0] == '/' &&
             ((tmp[1] >= 'A' && tmp[1] <= 'Z') ||
              (tmp[1] >= 'a' && tmp[1] <= 'z'))))
            continue;
        tmp[i] = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            return -1;
        tmp[i] = '/';
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int e9lr_copy_file(const char *src, const char *dst)
{
    int infd = -1;
    int outfd = -1;
    char buf[4096];
    ssize_t got;

    infd = open(src, O_RDONLY);
    if (infd < 0)
        return -1;
    outfd = open(dst, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (outfd < 0) {
        close(infd);
        return -1;
    }

    while ((got = read(infd, buf, sizeof(buf))) > 0) {
        char *p = buf;
        ssize_t left = got;
        while (left > 0) {
            ssize_t wrote = write(outfd, p, (size_t)left);
            if (wrote < 0) {
                close(infd);
                close(outfd);
                return -1;
            }
            p += wrote;
            left -= wrote;
        }
    }

    close(infd);
    close(outfd);
    return got < 0 ? -1 : 0;
}

static int e9lr_write_text_file(const char *path, const char *contents, mode_t mode)
{
    FILE *out = fopen(path, "wb");
    if (!out)
        return -1;
    if (contents && fputs(contents, out) == EOF) {
        fclose(out);
        return -1;
    }
    fclose(out);
    if (mode != 0)
        chmod(path, mode);
    return 0;
}

static uint8_t *e9lr_read_file(const char *path, size_t *out_size)
{
    struct stat st;
    int fd = open(path, O_RDONLY);
    uint8_t *buf;

    if (fd < 0)
        return NULL;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }
    buf = malloc((size_t)st.st_size);
    if (!buf) {
        close(fd);
        return NULL;
    }
    if (read(fd, buf, (size_t)st.st_size) != st.st_size) {
        free(buf);
        close(fd);
        return NULL;
    }
    close(fd);
    *out_size = (size_t)st.st_size;
    return buf;
}

static void e9lr_record_event(E9LiveReloadBddWorld *world,
                              E9LiveReloadEventType type,
                              const char *file_path,
                              uint32_t patch_id,
                              const char *function_name,
                              uint64_t patch_address,
                              size_t patch_size,
                              int error_code,
                              const char *error_msg)
{
    E9LiveReloadEventRecord *record;

    if (world->event_count >= E9LR_BDD_MAX_EVENTS)
        return;

    record = &world->events[world->event_count++];
    memset(record, 0, sizeof(*record));
    record->type = type;
    record->patch_id = patch_id;
    record->patch_address = patch_address;
    record->patch_size = patch_size;
    record->error_code = error_code;
    if (file_path)
        strncpy(record->file_path, file_path, sizeof(record->file_path) - 1);
    if (function_name)
        strncpy(record->function_name, function_name, sizeof(record->function_name) - 1);
    if (error_msg)
        strncpy(record->error_msg, error_msg, sizeof(record->error_msg) - 1);
}

static void e9lr_event_callback(const E9LiveReloadEvent *event, void *userdata)
{
    E9LiveReloadBddWorld *world = userdata;
    if (!event || !world)
        return;
    e9lr_record_event(world,
                      event->type,
                      event->file_path,
                      event->patch_id,
                      event->function_name,
                      event->patch_address,
                      event->patch_size,
                      event->error_code,
                      event->error_msg);
}

static void e9lr_clear_transient(E9LiveReloadBddWorld *world)
{
    if (e9_livereload_is_ready())
        e9_livereload_shutdown();
    if (world->ape_buffer) {
        free(world->ape_buffer);
        world->ape_buffer = NULL;
    }

    memset(&world->config, 0, sizeof(world->config));
    memset(world->events, 0, sizeof(world->events));
    world->event_count = 0;
    world->last_init_result = 0;
    world->last_reload_result = 0;
    world->last_watch_result = 0;
    world->last_poll_result = 0;
    world->last_compiler_available = false;
    world->last_patch_id = 0;
    world->last_revert_result = 0;
    memset(&world->last_stats, 0, sizeof(world->last_stats));
    world->patch_target_type = E9_PATCH_TARGET_FILE_OFFSET;
    world->manual_patch_address = 0;
    world->manual_patch_size = 0;
    world->resolved_file_offset = -1;
    world->patch_succeeded = false;
    world->zipos_warning_seen = false;
    world->execution_continues = false;
    memset(world->patch_bytes, 0, sizeof(world->patch_bytes));
    world->patch_bytes_size = 0;
    memset(world->saved_original_bytes, 0, sizeof(world->saved_original_bytes));
    world->saved_original_size = 0;
    world->ape_buffer_size = 0;
    memset(&world->ape_fixture, 0, sizeof(world->ape_fixture));
    world->diff_result = 0;
    world->diff_patch_count = 0;
    world->diff_function_name[0] = '\0';
    world->diff_patch_address = 0;
    memset(world->diff_old_bytes, 0, sizeof(world->diff_old_bytes));
    memset(world->diff_new_bytes, 0, sizeof(world->diff_new_bytes));
    world->diff_patch_size = 0;
    world->source_path[0] = '\0';
    world->source_path_sh[0] = '\0';
    world->last_status[0] = '\0';
    world->last_error[0] = '\0';
    e9lr_test_stubs_reset();
}

static int e9lr_configure_paths(E9LiveReloadBddWorld *world)
{
    char tmp[512];

    world->scenario_serial++;
    snprintf(world->workspace_dir, sizeof(world->workspace_dir),
             "%s/build/e9livereload_bdd_workspace/scenario_%ld_%02d",
             world->repo_root, (long)getpid(), world->scenario_serial);
    snprintf(world->source_dir, sizeof(world->source_dir), "%s/src", world->workspace_dir);
    snprintf(world->cache_dir, sizeof(world->cache_dir), "%s/.e9cache", world->workspace_dir);
    snprintf(world->bin_dir, sizeof(world->bin_dir), "%s/bin", world->workspace_dir);
    snprintf(world->tmp_dir, sizeof(world->tmp_dir), "%s/tmp", world->workspace_dir);
    snprintf(world->target_path, sizeof(world->target_path), "%s/target.com", world->workspace_dir);
    snprintf(world->missing_target_path, sizeof(world->missing_target_path), "%s/missing-target.com",
             world->workspace_dir);
    snprintf(world->compiler_script_path, sizeof(world->compiler_script_path), "%s/cosmocc", world->bin_dir);
    snprintf(world->compiler_invocation_log, sizeof(world->compiler_invocation_log), "%s/cosmocc.invocations.log",
             world->bin_dir);
    snprintf(world->compiler_output_log, sizeof(world->compiler_output_log), "%s/cosmocc.output.log",
             world->bin_dir);
    snprintf(world->which_script_path, sizeof(world->which_script_path), "%s/which", world->bin_dir);

    e9lr_to_shell_path(world->workspace_dir, world->workspace_dir_sh, sizeof(world->workspace_dir_sh));
    e9lr_to_shell_path(world->source_dir, world->source_dir_sh, sizeof(world->source_dir_sh));
    e9lr_to_shell_path(world->cache_dir, world->cache_dir_sh, sizeof(world->cache_dir_sh));
    e9lr_to_shell_path(world->bin_dir, world->bin_dir_sh, sizeof(world->bin_dir_sh));
    e9lr_to_shell_path(world->tmp_dir, world->tmp_dir_sh, sizeof(world->tmp_dir_sh));
    e9lr_to_shell_path(world->target_path, world->target_path_sh, sizeof(world->target_path_sh));
    e9lr_to_shell_path(world->missing_target_path, world->missing_target_path_sh,
                       sizeof(world->missing_target_path_sh));
    e9lr_to_shell_path(world->compiler_script_path, world->compiler_script_path_sh,
                       sizeof(world->compiler_script_path_sh));
    e9lr_to_shell_path(world->compiler_invocation_log, world->compiler_invocation_log_sh,
                       sizeof(world->compiler_invocation_log_sh));
    e9lr_to_shell_path(world->compiler_output_log, world->compiler_output_log_sh,
                       sizeof(world->compiler_output_log_sh));
    e9lr_to_shell_path(world->which_script_path, world->which_script_path_sh,
                       sizeof(world->which_script_path_sh));

    if (e9lr_ensure_dir(world->source_dir) != 0 ||
        e9lr_ensure_dir(world->cache_dir) != 0 ||
        e9lr_ensure_dir(world->bin_dir) != 0 ||
        e9lr_ensure_dir(world->tmp_dir) != 0) {
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s/build/e9_target_test.com", world->repo_root);
    if (e9lr_world_file_exists(tmp)) {
        strncpy(world->target_fixture_path, tmp, sizeof(world->target_fixture_path) - 1);
    } else {
        snprintf(world->target_fixture_path, sizeof(world->target_fixture_path),
                 "%s/build/opensmithgen.com", world->repo_root);
    }
    e9lr_to_shell_path(world->target_fixture_path, world->target_fixture_path_sh,
                       sizeof(world->target_fixture_path_sh));
    return e9lr_copy_file(world->target_fixture_path, world->target_path);
}

int e9lr_world_restore_compiler(E9LiveReloadBddWorld *world)
{
    char script[2048];
    const char *which_script =
        "#!/bin/sh\n"
        "if [ $# -lt 1 ]; then\n"
        "  exit 1\n"
        "fi\n"
        "if [ -x \"$1\" ]; then\n"
        "  printf '%s\\n' \"$1\"\n"
        "  exit 0\n"
        "fi\n"
        "exit 1\n";

    if (e9lr_write_text_file(world->which_script_path, which_script, 0755) != 0)
        return -1;

    snprintf(script, sizeof(script),
             "#!/bin/sh\n"
             "printf '%%s\\n' \"$*\" >>'%s'\n"
             "if [ -x /C/bin/sh.exe ] && [ -x \"$HOME/.cosmocc/bin/cosmocc\" ]; then\n"
             "  /C/bin/sh.exe \"$HOME/.cosmocc/bin/cosmocc\" \"$@\" >>'%s' 2>&1\n"
             "  exit $?\n"
             "fi\n"
             "if command -v cosmocc >/dev/null 2>&1; then\n"
             "  cosmocc \"$@\" >>'%s' 2>&1\n"
             "  exit $?\n"
             "fi\n"
             "echo 'cosmocc unavailable' >>'%s'\n"
             "exit 127\n",
             world->compiler_invocation_log_sh,
             world->compiler_output_log_sh,
             world->compiler_output_log_sh,
             world->compiler_output_log_sh);
    return e9lr_write_text_file(world->compiler_script_path, script, 0755);
}

int e9lr_world_remove_compiler(E9LiveReloadBddWorld *world)
{
    unlink(world->compiler_script_path);
    return 0;
}

int e9lr_world_init(E9LiveReloadBddWorld *world)
{
    char cwd[512];
    const char *path_env;
    const char *tmpdir_env;
    const char *self;

    memset(world, 0, sizeof(*world));
    if (!getcwd(cwd, sizeof(cwd)))
        return -1;
    strncpy(world->repo_root, cwd, sizeof(world->repo_root) - 1);
    world->repo_root[sizeof(world->repo_root) - 1] = '\0';
    e9lr_normalize_path(world->repo_root);

    path_env = getenv("PATH");
    tmpdir_env = getenv("TMPDIR");
    if (path_env)
        strncpy(world->original_path, path_env, sizeof(world->original_path) - 1);
    if (tmpdir_env)
        strncpy(world->original_tmpdir, tmpdir_env, sizeof(world->original_tmpdir) - 1);

    self = e9_ape_get_self_path();
    if (self)
        strncpy(world->runner_path, self, sizeof(world->runner_path) - 1);
    e9lr_test_stubs_reset();
    return 0;
}

void e9lr_world_destroy(E9LiveReloadBddWorld *world)
{
    if (e9_livereload_is_ready())
        e9_livereload_shutdown();
    if (world->ape_buffer)
        free(world->ape_buffer);
    if (world->original_path[0])
        setenv("PATH", world->original_path, 1);
    if (world->original_tmpdir[0])
        setenv("TMPDIR", world->original_tmpdir, 1);
}

int e9lr_world_reset_scenario(E9LiveReloadBddWorld *world)
{
    char scoped_path[1024];

    e9lr_clear_transient(world);
    if (e9lr_configure_paths(world) != 0)
        return -1;
    if (e9lr_world_restore_compiler(world) != 0)
        return -1;

    snprintf(scoped_path, sizeof(scoped_path), "%s:/C/bin", world->bin_dir_sh);
    setenv("PATH", scoped_path, 1);
    setenv("TMPDIR", world->tmp_dir_sh, 1);

    world->config = (E9LiveReloadConfig)E9_LIVERELOAD_CONFIG_DEFAULT;
    world->config.source_dir = world->source_dir_sh;
    world->config.compiler = world->compiler_script_path_sh;
    world->config.compiler_flags = "-O2 -g";
    world->config.cache_dir = world->cache_dir_sh;
    world->config.enable_hot_patch = true;
    world->config.enable_file_patch = false;
    world->config.verbose = false;
    return 0;
}

void e9lr_world_attach_callback(E9LiveReloadBddWorld *world)
{
    e9_livereload_set_callback(e9lr_event_callback, world);
}

static int e9lr_resolve_source_paths(E9LiveReloadBddWorld *world,
                                     const char *relative_path,
                                     char *out_path,
                                     char *out_path_sh)
{
    const char *suffix = relative_path;
    if (strncmp(relative_path, "src/", 4) == 0)
        suffix = relative_path + 4;

    snprintf(out_path, 512, "%s/%s", world->source_dir, suffix);
    e9lr_to_shell_path(out_path, out_path_sh, 512);

    {
        char dir[512];
        strncpy(dir, out_path, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            if (e9lr_ensure_dir(dir) != 0)
                return -1;
        }
    }

    return 0;
}

int e9lr_world_write_source(E9LiveReloadBddWorld *world, const char *relative_path, const char *contents)
{
    if (e9lr_resolve_source_paths(world, relative_path, world->source_path, world->source_path_sh) != 0)
        return -1;
    return e9lr_write_text_file(world->source_path, contents, 0644);
}

int e9lr_world_append_source(E9LiveReloadBddWorld *world, const char *relative_path, const char *suffix)
{
    FILE *out;
    if (e9lr_resolve_source_paths(world, relative_path, world->source_path, world->source_path_sh) != 0)
        return -1;
    out = fopen(world->source_path, "ab");
    if (!out)
        return -1;
    if (suffix && fputs(suffix, out) == EOF) {
        fclose(out);
        return -1;
    }
    fclose(out);
    return 0;
}

int e9lr_world_read_text(const char *path, char *buf, size_t buf_size)
{
    FILE *in = fopen(path, "rb");
    size_t got;
    if (!in)
        return -1;
    got = fread(buf, 1, buf_size - 1, in);
    fclose(in);
    buf[got] = '\0';
    return 0;
}

bool e9lr_world_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

size_t e9lr_world_count_events(const E9LiveReloadBddWorld *world, E9LiveReloadEventType type)
{
    size_t count = 0;
    for (size_t i = 0; i < world->event_count; ++i) {
        if (world->events[i].type == type)
            count++;
    }
    return count;
}

const E9LiveReloadEventRecord *e9lr_world_find_event(const E9LiveReloadBddWorld *world,
                                                     E9LiveReloadEventType type,
                                                     size_t occurrence)
{
    size_t seen = 0;
    for (size_t i = 0; i < world->event_count; ++i) {
        if (world->events[i].type != type)
            continue;
        if (seen == occurrence)
            return &world->events[i];
        seen++;
    }
    return NULL;
}

void e9lr_world_capture_error(E9LiveReloadBddWorld *world)
{
    const char *err = e9_livereload_get_error();
    world->last_error[0] = '\0';
    if (err)
        strncpy(world->last_error, err, sizeof(world->last_error) - 1);
}

int e9lr_world_diff_cached_objects(E9LiveReloadBddWorld *world,
                                   const char *old_name,
                                   const char *new_name)
{
    char old_path[512];
    char new_path[512];
    size_t old_size = 0;
    size_t new_size = 0;
    uint8_t *old_bytes;
    uint8_t *new_bytes;
    E9BinaryenPatch *patches = NULL;
    int patch_count = 0;

    snprintf(old_path, sizeof(old_path), "%s/%s", world->cache_dir, old_name);
    snprintf(new_path, sizeof(new_path), "%s/%s", world->cache_dir, new_name);

    old_bytes = e9lr_read_file(old_path, &old_size);
    new_bytes = e9lr_read_file(new_path, &new_size);
    if (!old_bytes || !new_bytes) {
        free(old_bytes);
        free(new_bytes);
        return -1;
    }

    world->diff_result = e9_binaryen_diff_objects(old_bytes, old_size, new_bytes, new_size,
                                                  &patches, &patch_count);
    free(old_bytes);
    free(new_bytes);
    if (world->diff_result != 0)
        return world->diff_result;

    world->diff_patch_count = patch_count;
    if (patch_count > 0) {
        strncpy(world->diff_function_name, patches[0].function ? patches[0].function : "",
                sizeof(world->diff_function_name) - 1);
        world->diff_patch_address = patches[0].address;
        world->diff_patch_size = patches[0].size;
        if (patches[0].size <= sizeof(world->diff_old_bytes)) {
            memcpy(world->diff_old_bytes, patches[0].old_bytes, patches[0].size);
            memcpy(world->diff_new_bytes, patches[0].new_bytes, patches[0].size);
        }
        e9lr_record_event(world, E9_LR_EVENT_PATCH_GENERATED, NULL, 1,
                          world->diff_function_name, world->diff_patch_address,
                          world->diff_patch_size, 0, NULL);
    } else {
        strncpy(world->last_status, "No changes detected", sizeof(world->last_status) - 1);
    }

    e9_binaryen_free_patches(patches, patch_count);
    return 0;
}

int e9lr_world_prepare_ape_fixture(E9LiveReloadBddWorld *world,
                                   off_t text_offset,
                                   uint32_t text_rva,
                                   size_t text_size,
                                   off_t rdata_offset,
                                   uint32_t rdata_rva,
                                   size_t rdata_size,
                                   off_t zipos_start)
{
    size_t required = 0x70080;

    if (world->ape_buffer) {
        free(world->ape_buffer);
        world->ape_buffer = NULL;
    }

    memset(&world->ape_fixture, 0, sizeof(world->ape_fixture));
    world->ape_fixture.text_offset = text_offset;
    world->ape_fixture.text_rva = text_rva;
    world->ape_fixture.text_size = text_size;
    world->ape_fixture.rdata_offset = rdata_offset;
    world->ape_fixture.rdata_rva = rdata_rva;
    world->ape_fixture.rdata_size = rdata_size;
    world->ape_fixture.zipos_start = zipos_start;
    world->ape_fixture.preserve_zipos = zipos_start > 0;

    if ((size_t)(text_offset + (off_t)text_size + 0x100) > required)
        required = (size_t)(text_offset + (off_t)text_size + 0x100);
    if ((size_t)(rdata_offset + (off_t)rdata_size + 0x100) > required)
        required = (size_t)(rdata_offset + (off_t)rdata_size + 0x100);
    if (zipos_start > 0 && (size_t)(zipos_start + 0x100) > required)
        required = (size_t)(zipos_start + 0x100);

    world->ape_buffer = calloc(1, required);
    if (!world->ape_buffer)
        return -1;
    memset(world->ape_buffer, 0xA5, required);
    world->ape_buffer_size = required;
    return 0;
}

int e9lr_world_apply_manual_patch(E9LiveReloadBddWorld *world)
{
    switch (world->patch_target_type) {
        case E9_PATCH_TARGET_FILE_OFFSET:
            world->resolved_file_offset = (off_t)world->manual_patch_address;
            break;
        case E9_PATCH_TARGET_PE_RVA:
            world->resolved_file_offset = e9_ape_rva_to_offset(&world->ape_fixture,
                                                               (uint32_t)world->manual_patch_address);
            break;
        case E9_PATCH_TARGET_VA:
            world->resolved_file_offset = e9_ape_rva_to_offset(&world->ape_fixture,
                                                               (uint32_t)(world->manual_patch_address - 0x400000));
            break;
    }

    if (world->resolved_file_offset < 0)
        return -1;
    if ((size_t)(world->resolved_file_offset + (off_t)world->patch_bytes_size) > world->ape_buffer_size)
        return -1;

    world->saved_original_size = world->patch_bytes_size;
    memcpy(world->saved_original_bytes,
           world->ape_buffer + world->resolved_file_offset,
           world->patch_bytes_size);
    if (world->ape_fixture.preserve_zipos &&
        world->ape_fixture.zipos_start > 0 &&
        world->resolved_file_offset + (off_t)world->patch_bytes_size > world->ape_fixture.zipos_start) {
        world->zipos_warning_seen = true;
    }

    if (e9_ape_patch_offset(world->ape_buffer, world->ape_buffer_size,
                            &world->ape_fixture, world->resolved_file_offset,
                            world->patch_bytes, world->patch_bytes_size) != 0) {
        world->patch_succeeded = false;
        return -1;
    }

    world->patch_succeeded = true;
    world->manual_patch_size = world->patch_bytes_size;
    e9wasm_flush_icache(world->ape_buffer + world->resolved_file_offset,
                        world->patch_bytes_size);
    e9lr_record_event(world, E9_LR_EVENT_PATCH_APPLIED, NULL, 1, NULL,
                      world->manual_patch_address, world->patch_bytes_size, 0, NULL);
    return 0;
}

int e9lr_world_read_bytes(const char *path, off_t offset, uint8_t *buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }
    if (read(fd, buf, size) != (ssize_t)size) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}
