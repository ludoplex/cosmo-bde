#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wasm/e9binaryen.h"
#include "wasm/e9wasm_host.h"

typedef struct {
    bool ready;
    bool no_changes;
    char function_name[128];
    uint64_t address;
    uint8_t old_bytes[64];
    uint8_t new_bytes[64];
    size_t size;
    char error[256];
} E9BinaryenStubState;

typedef struct {
    size_t count;
    void *last_addr;
    size_t last_size;
} E9FlushStubState;

static E9BinaryenStubState g_binaryen_stub;
static E9FlushStubState g_flush_stub;

void e9lr_test_stubs_reset(void)
{
    memset(&g_binaryen_stub, 0, sizeof(g_binaryen_stub));
    memset(&g_flush_stub, 0, sizeof(g_flush_stub));
    g_binaryen_stub.address = 0x11234;
    memcpy(g_binaryen_stub.old_bytes, "\x90\x90", 2);
    memcpy(g_binaryen_stub.new_bytes, "\xCC\xCC", 2);
    g_binaryen_stub.size = 2;
    strcpy(g_binaryen_stub.function_name, "process_data");
}

void e9lr_test_stubs_configure_binaryen_patch(const char *function_name,
                                              uint64_t address,
                                              const uint8_t *old_bytes,
                                              const uint8_t *new_bytes,
                                              size_t size)
{
    memset(g_binaryen_stub.old_bytes, 0, sizeof(g_binaryen_stub.old_bytes));
    memset(g_binaryen_stub.new_bytes, 0, sizeof(g_binaryen_stub.new_bytes));
    g_binaryen_stub.address = address;
    g_binaryen_stub.size = size;
    if (function_name && *function_name) {
        strncpy(g_binaryen_stub.function_name, function_name,
                sizeof(g_binaryen_stub.function_name) - 1);
        g_binaryen_stub.function_name[sizeof(g_binaryen_stub.function_name) - 1] = '\0';
    }
    if (old_bytes && size <= sizeof(g_binaryen_stub.old_bytes))
        memcpy(g_binaryen_stub.old_bytes, old_bytes, size);
    if (new_bytes && size <= sizeof(g_binaryen_stub.new_bytes))
        memcpy(g_binaryen_stub.new_bytes, new_bytes, size);
    g_binaryen_stub.no_changes = false;
}

void e9lr_test_stubs_configure_binaryen_no_changes(bool no_changes)
{
    g_binaryen_stub.no_changes = no_changes;
}

size_t e9lr_test_stubs_get_flush_count(void)
{
    return g_flush_stub.count;
}

void *e9lr_test_stubs_get_last_flush_addr(void)
{
    return g_flush_stub.last_addr;
}

size_t e9lr_test_stubs_get_last_flush_size(void)
{
    return g_flush_stub.last_size;
}

int e9_binaryen_init(E9BinaryenBackend backend)
{
    (void)backend;
    g_binaryen_stub.ready = true;
    g_binaryen_stub.error[0] = '\0';
    return 0;
}

void e9_binaryen_shutdown(void)
{
    g_binaryen_stub.ready = false;
}

bool e9_binaryen_is_ready(void)
{
    return g_binaryen_stub.ready;
}

const char *e9_binaryen_version(void)
{
    return "e9livereload-bdd-stub";
}

int e9_binaryen_diff_objects(const uint8_t *old_obj, size_t old_size,
                             const uint8_t *new_obj, size_t new_size,
                             E9BinaryenPatch **out_patches,
                             int *out_num_patches)
{
    if (!out_patches || !out_num_patches) {
        strncpy(g_binaryen_stub.error, "Invalid diff output pointers",
                sizeof(g_binaryen_stub.error) - 1);
        return -1;
    }

    *out_patches = NULL;
    *out_num_patches = 0;

    if (g_binaryen_stub.no_changes ||
        (old_size == new_size && old_obj && new_obj &&
         memcmp(old_obj, new_obj, old_size) == 0)) {
        return 0;
    }

    E9BinaryenPatch *patch = calloc(1, sizeof(*patch));
    if (!patch) {
        strncpy(g_binaryen_stub.error, "Out of memory",
                sizeof(g_binaryen_stub.error) - 1);
        return -1;
    }

    patch->address = g_binaryen_stub.address;
    patch->size = g_binaryen_stub.size;
    patch->function = strdup(g_binaryen_stub.function_name);
    patch->old_bytes = malloc(g_binaryen_stub.size);
    patch->new_bytes = malloc(g_binaryen_stub.size);
    if (!patch->function || !patch->old_bytes || !patch->new_bytes) {
        free((void *)patch->function);
        free(patch->old_bytes);
        free(patch->new_bytes);
        free(patch);
        strncpy(g_binaryen_stub.error, "Out of memory",
                sizeof(g_binaryen_stub.error) - 1);
        return -1;
    }

    memcpy(patch->old_bytes, g_binaryen_stub.old_bytes, g_binaryen_stub.size);
    memcpy(patch->new_bytes, g_binaryen_stub.new_bytes, g_binaryen_stub.size);
    *out_patches = patch;
    *out_num_patches = 1;
    return 0;
}

void e9_binaryen_free_patches(E9BinaryenPatch *patches, int num_patches)
{
    if (!patches)
        return;

    for (int i = 0; i < num_patches; i++) {
        free((void *)patches[i].function);
        free(patches[i].old_bytes);
        free(patches[i].new_bytes);
    }
    free(patches);
}

const char *e9_binaryen_get_error(void)
{
    return g_binaryen_stub.error[0] ? g_binaryen_stub.error : NULL;
}

void e9_binaryen_clear_error(void)
{
    g_binaryen_stub.error[0] = '\0';
}

void e9wasm_flush_icache(void *addr, size_t size)
{
    g_flush_stub.count++;
    g_flush_stub.last_addr = addr;
    g_flush_stub.last_size = size;
}
