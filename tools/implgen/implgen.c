/* cosmo-bde — Implementation Directive Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates platform dispatch and optimization hints from .impl files.
 * Output is pure C with Cosmopolitan-aware platform detection.
 *
 * TRUE DOGFOODING: Uses implgen_self.h which expands implgen_tokens.def
 * via X-macros to define this generator's own token types.
 *
 * Usage: implgen <input.impl> [output_dir] [prefix]
 *
 * Input format:
 *   impl platform funcname {
 *       linux: "impl/linux.c"
 *       windows: "impl/windows.c"
 *       cosmo: "impl/cosmo.c"
 *   }
 *
 *   impl simd {
 *       target: [avx2, sse4, neon]
 *       fallback: scalar
 *   }
 *
 * Output:
 *   <prefix>_impl.h  — Platform dispatch macros
 */

#include <stdio.h>
#include "../cosmo_harden.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

/* ── Self-hosted tokens (dogfooding) ─────────────────────────────── */
#include "implgen_self.h"

#define IMPLGEN_VERSION "1.0.0"
#define MAX_PATH 512
#define MAX_NAME 64
#define MAX_LINE 1024
#define MAX_PLATFORMS 16
#define MAX_TARGETS 8

/* ── Data Structures ─────────────────────────────────────────────── */

typedef struct {
    char name[MAX_NAME];
    char file[MAX_PATH];
    int priority;
} platform_target_t;

typedef struct {
    char name[MAX_NAME];
    platform_target_t targets[MAX_PLATFORMS];
    int target_count;
    char fallback[MAX_PATH];
} platform_dispatch_t;

typedef struct {
    char targets[MAX_TARGETS][MAX_NAME];
    int target_count;
    char fallback[MAX_NAME];
    int runtime_detect;
} simd_config_t;

static platform_dispatch_t dispatches[64];
static int dispatch_count = 0;
static simd_config_t simd;

/* ── Utilities ────────────────────────────────────────────────────── */

static void trim(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
}

static void to_upper(char *s) {
    for (; *s; s++) *s = toupper((unsigned char)*s);
}

static int ensure_output_dir(const char *outdir) {
    struct stat st;
    if (stat(outdir, &st) == 0) return 0;
    return mkdir(outdir, 0755);
}

/* ── Parser ──────────────────────────────────────────────────────── */

static int parse_impl(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", filename, strerror(errno));
        return -1;
    }

    char line[MAX_LINE];
    int in_platform = 0, in_simd = 0;
    platform_dispatch_t *current = NULL;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        /* impl platform name { */
        if (strncmp(line, "impl platform", 13) == 0) {
            char *name = line + 13;
            while (*name && isspace((unsigned char)*name)) name++;
            char *brace = strchr(name, '{');
            if (brace) *brace = '\0';
            trim(name);

            current = &dispatches[dispatch_count++];
            strncpy(current->name, name, MAX_NAME - 1);
            current->target_count = 0;
            in_platform = 1;
            continue;
        }

        /* impl simd { */
        if (strncmp(line, "impl simd", 9) == 0) {
            simd.target_count = 0;
            strcpy(simd.fallback, "scalar");
            simd.runtime_detect = 1;
            in_simd = 1;
            continue;
        }

        /* } end block */
        if (line[0] == '}') {
            in_platform = 0;
            in_simd = 0;
            current = NULL;
            continue;
        }

        /* platform: "file" */
        if (in_platform && current) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *platform = line;
                char *file = colon + 1;
                trim(platform);
                trim(file);
                /* Remove quotes */
                if (file[0] == '"') file++;
                char *end = strchr(file, '"');
                if (end) *end = '\0';

                platform_target_t *t = &current->targets[current->target_count++];
                strncpy(t->name, platform, MAX_NAME - 1);
                strncpy(t->file, file, MAX_PATH - 1);
            }
        }

        /* simd targets */
        if (in_simd) {
            if (strncmp(line, "target:", 7) == 0) {
                char *list = line + 7;
                trim(list);
                if (list[0] == '[') list++;
                char *end = strchr(list, ']');
                if (end) *end = '\0';

                char *tok = strtok(list, ",");
                while (tok && simd.target_count < MAX_TARGETS) {
                    trim(tok);
                    strncpy(simd.targets[simd.target_count++], tok, MAX_NAME - 1);
                    tok = strtok(NULL, ",");
                }
            }
            if (strncmp(line, "fallback:", 9) == 0) {
                char *val = line + 9;
                trim(val);
                strncpy(simd.fallback, val, MAX_NAME - 1);
            }
        }
    }

    fclose(f);
    return 0;
}

/* ── Code Generation ─────────────────────────────────────────────── */

static int generate_impl_h(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s_impl.h", outdir, prefix);

    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    char upper[MAX_NAME];
    strncpy(upper, prefix, MAX_NAME - 1);
    to_upper(upper);

    time_t now = time(NULL);
    fprintf(out, "/* AUTO-GENERATED by implgen %s — DO NOT EDIT\n", IMPLGEN_VERSION);
    fprintf(out, " * @generated %s", ctime(&now));
    fprintf(out, " * Regenerate: make regen\n");
    fprintf(out, " */\n\n");
    fprintf(out, "#ifndef %s_IMPL_H\n", upper);
    fprintf(out, "#define %s_IMPL_H\n\n", upper);

    /* Platform detection (Cosmopolitan-aware) */
    fprintf(out, "/* ── Platform Detection (Cosmopolitan-aware) ────────────────────── */\n\n");
    fprintf(out, "#ifdef __COSMOPOLITAN__\n");
    fprintf(out, "  #include \"libc/runtime/runtime.h\"\n");
    fprintf(out, "  #define %s_IS_LINUX    IsLinux()\n", upper);
    fprintf(out, "  #define %s_IS_WINDOWS  IsWindows()\n", upper);
    fprintf(out, "  #define %s_IS_MACOS    IsXnu()\n", upper);
    fprintf(out, "  #define %s_IS_COSMO    1\n", upper);
    fprintf(out, "#else\n");
    fprintf(out, "  #if defined(__linux__)\n");
    fprintf(out, "    #define %s_IS_LINUX    1\n", upper);
    fprintf(out, "    #define %s_IS_WINDOWS  0\n", upper);
    fprintf(out, "    #define %s_IS_MACOS    0\n", upper);
    fprintf(out, "  #elif defined(_WIN32)\n");
    fprintf(out, "    #define %s_IS_LINUX    0\n", upper);
    fprintf(out, "    #define %s_IS_WINDOWS  1\n", upper);
    fprintf(out, "    #define %s_IS_MACOS    0\n", upper);
    fprintf(out, "  #elif defined(__APPLE__)\n");
    fprintf(out, "    #define %s_IS_LINUX    0\n", upper);
    fprintf(out, "    #define %s_IS_WINDOWS  0\n", upper);
    fprintf(out, "    #define %s_IS_MACOS    1\n", upper);
    fprintf(out, "  #endif\n");
    fprintf(out, "  #define %s_IS_COSMO    0\n", upper);
    fprintf(out, "#endif\n\n");

    /* Platform dispatch macros */
    fprintf(out, "/* ── Platform Dispatch ────────────────────────────────────────────── */\n\n");
    for (int i = 0; i < dispatch_count; i++) {
        platform_dispatch_t *d = &dispatches[i];
        char func_upper[MAX_NAME];
        strncpy(func_upper, d->name, MAX_NAME - 1);
        to_upper(func_upper);

        fprintf(out, "/* Dispatch for %s */\n", d->name);
        fprintf(out, "#if %s_IS_COSMO\n", upper);

        /* Cosmopolitan runtime dispatch */
        int has_linux = 0, has_windows = 0;
        for (int j = 0; j < d->target_count; j++) {
            if (strcmp(d->targets[j].name, "linux") == 0) has_linux = 1;
            if (strcmp(d->targets[j].name, "windows") == 0) has_windows = 1;
        }

        if (has_linux && has_windows) {
            fprintf(out, "  #define %s_%s_IMPL() \\\n", upper, func_upper);
            fprintf(out, "      (IsWindows() ? %s_%s_windows() : %s_%s_linux())\n",
                    prefix, d->name, prefix, d->name);
        } else if (d->target_count > 0) {
            fprintf(out, "  #define %s_%s_IMPL() %s_%s_%s()\n",
                    upper, func_upper, prefix, d->name, d->targets[0].name);
        }

        /* Platform-specific */
        for (int j = 0; j < d->target_count; j++) {
            char plat_upper[MAX_NAME];
            strncpy(plat_upper, d->targets[j].name, MAX_NAME - 1);
            to_upper(plat_upper);

            fprintf(out, "#elif %s_IS_%s\n", upper, plat_upper);
            fprintf(out, "  #define %s_%s_IMPL() %s_%s_%s()\n",
                    upper, func_upper, prefix, d->name, d->targets[j].name);
        }
        fprintf(out, "#endif\n\n");
    }

    /* SIMD dispatch */
    if (simd.target_count > 0) {
        fprintf(out, "/* ── SIMD Dispatch ─────────────────────────────────────────────────── */\n\n");
        fprintf(out, "typedef enum {\n");
        fprintf(out, "    %s_SIMD_SCALAR = 0,\n", upper);
        for (int i = 0; i < simd.target_count; i++) {
            char simd_upper[MAX_NAME];
            strncpy(simd_upper, simd.targets[i], MAX_NAME - 1);
            to_upper(simd_upper);
            fprintf(out, "    %s_SIMD_%s,\n", upper, simd_upper);
        }
        fprintf(out, "    %s_SIMD_COUNT\n", upper);
        fprintf(out, "} %s_simd_t;\n\n", prefix);

        fprintf(out, "/* Runtime SIMD detection */\n");
        fprintf(out, "static inline %s_simd_t %s_detect_simd(void) {\n", prefix, prefix);
        fprintf(out, "#if defined(__AVX2__)\n");
        fprintf(out, "    return %s_SIMD_AVX2;\n", upper);
        fprintf(out, "#elif defined(__AVX__)\n");
        fprintf(out, "    return %s_SIMD_AVX;\n", upper);
        fprintf(out, "#elif defined(__SSE4_1__)\n");
        fprintf(out, "    return %s_SIMD_SSE4;\n", upper);
        fprintf(out, "#elif defined(__ARM_NEON)\n");
        fprintf(out, "    return %s_SIMD_NEON;\n", upper);
        fprintf(out, "#else\n");
        fprintf(out, "    return %s_SIMD_SCALAR;\n", upper);
        fprintf(out, "#endif\n");
        fprintf(out, "}\n\n");
    }

    fprintf(out, "#endif /* %s_IMPL_H */\n", upper);
    fclose(out);

    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

/* ── Main ─────────────────────────────────────────────────────────── */

static void print_usage(void) {
    fprintf(stderr, "implgen %s — Implementation Directive Generator\n", IMPLGEN_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: implgen <input.impl> [output_dir] [prefix]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Generates platform dispatch and SIMD selection from .impl specs.\n");
    fprintf(stderr, "Output is Cosmopolitan-aware with runtime platform detection.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Output:\n");
    fprintf(stderr, "  <prefix>_impl.h  — Platform dispatch macros\n");
}

int main(int argc, char *argv[]) {
    tool_harden();
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *input = argv[1];
    const char *outdir = argc > 2 ? argv[2] : ".";

    /* Derive prefix from filename */
    char prefix[MAX_NAME];
    const char *basename = strrchr(input, '/');
    basename = basename ? basename + 1 : input;
    strncpy(prefix, basename, MAX_NAME - 1);
    char *dot = strchr(prefix, '.');
    if (dot) *dot = '\0';

    if (argc > 3) {
        strncpy(prefix, argv[3], MAX_NAME - 1);
    }

    if (parse_impl(input) != 0) {
        return 1;
    }

    fprintf(stderr, "Parsed %d platform dispatches from %s\n", dispatch_count, input);

    if (ensure_output_dir(outdir) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: Cannot create output directory %s\n", outdir);
        return 1;
    }

    if (generate_impl_h(outdir, prefix) != 0) return 1;

    return 0;
}
