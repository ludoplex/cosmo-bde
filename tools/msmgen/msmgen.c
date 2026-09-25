/* cosmo-bde — Modal State Machine Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates mode-switching state machines from .msm specs.
 * Modes are mutually exclusive top-level states.
 *
 * Usage: msmgen <input.msm> [output_dir] [prefix]
 *
 * Input:
 *   modal EditorModes {
 *       default: Normal
 *       mode Normal { on 'i' -> Insert; on ':' -> Command }
 *       mode Insert { on ESC -> Normal }
 *       mode Command { on ENTER -> Normal; on ESC -> Normal }
 *   }
 *
 * Output:
 *   <prefix>_msm.h  — Mode enum, transition table
 *   <prefix>_msm.c  — Mode switch implementation
 */

#include <stdio.h>
#include "../cosmo_harden.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#include "msmgen_self.h"

#define MSMGEN_VERSION "1.0.0"
#define MAX_PATH 512
#define MAX_NAME 64
#define MAX_LINE 1024
#define MAX_MODES 32
#define MAX_TRANS 64

typedef struct {
    char event[MAX_NAME];
    char target[MAX_NAME];
} transition_t;

typedef struct {
    char name[MAX_NAME];
    char entry[MAX_NAME];
    char exit_action[MAX_NAME];
    transition_t trans[MAX_TRANS];
    int trans_count;
} msm_mode_t;

typedef struct {
    char name[MAX_NAME];
    char default_mode[MAX_NAME];
    msm_mode_t modes[MAX_MODES];
    int mode_count;
} modal_t;

static modal_t machine;

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

static int parse_msm(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", filename, strerror(errno));
        return -1;
    }

    char line[MAX_LINE];
    msm_mode_t *current_mode = NULL;

    memset(&machine, 0, sizeof(machine));

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        /* modal Name { */
        if (strncmp(line, "modal ", 6) == 0) {
            char *name = line + 6;
            char *brace = strchr(name, '{');
            if (brace) *brace = '\0';
            trim(name);
            strncpy(machine.name, name, MAX_NAME - 1);
            continue;
        }

        /* default: ModeName */
        if (strncmp(line, "default:", 8) == 0) {
            char *mode = line + 8;
            trim(mode);
            strncpy(machine.default_mode, mode, MAX_NAME - 1);
            continue;
        }

        /* mode Name { */
        if (strncmp(line, "mode ", 5) == 0) {
            char *name = line + 5;
            char *brace = strchr(name, '{');
            if (brace) *brace = '\0';
            trim(name);

            current_mode = &machine.modes[machine.mode_count++];
            memset(current_mode, 0, sizeof(*current_mode));
            strncpy(current_mode->name, name, MAX_NAME - 1);
            continue;
        }

        /* } */
        if (line[0] == '}') {
            current_mode = NULL;
            continue;
        }

        /* on EVENT -> Target */
        if (current_mode && strncmp(line, "on ", 3) == 0) {
            char *p = line + 3;
            char *arrow = strstr(p, "->");
            if (arrow) {
                *arrow = '\0';
                char *event = p;
                char *target = arrow + 2;
                trim(event);
                trim(target);

                /* Remove trailing ; */
                char *semi = strchr(target, ';');
                if (semi) *semi = '\0';
                trim(target);

                transition_t *t = &current_mode->trans[current_mode->trans_count++];
                strncpy(t->event, event, MAX_NAME - 1);
                strncpy(t->target, target, MAX_NAME - 1);
            }
        }

        /* entry: action() */
        if (current_mode && strncmp(line, "entry:", 6) == 0) {
            char *action = line + 6;
            trim(action);
            strncpy(current_mode->entry, action, MAX_NAME - 1);
        }

        /* exit: action() */
        if (current_mode && strncmp(line, "exit:", 5) == 0) {
            char *action = line + 5;
            trim(action);
            strncpy(current_mode->exit_action, action, MAX_NAME - 1);
        }
    }

    fclose(f);
    return 0;
}

static int generate_msm_h(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s_msm.h", outdir, prefix);

    FILE *out = fopen(path, "w");
    if (!out) return -1;

    char upper[MAX_NAME];
    strncpy(upper, prefix, MAX_NAME - 1);
    to_upper(upper);

    time_t now = time(NULL);
    fprintf(out, "/* AUTO-GENERATED by msmgen %s — DO NOT EDIT\n", MSMGEN_VERSION);
    fprintf(out, " * @generated %s", ctime(&now));
    fprintf(out, " * Regenerate: make regen\n */\n\n");
    fprintf(out, "#ifndef %s_MSM_H\n#define %s_MSM_H\n\n", upper, upper);

    /* Mode enum */
    fprintf(out, "typedef enum {\n");
    for (int i = 0; i < machine.mode_count; i++) {
        char mode_upper[MAX_NAME];
        strncpy(mode_upper, machine.modes[i].name, MAX_NAME - 1);
        to_upper(mode_upper);
        fprintf(out, "    %s_MODE_%s%s\n", upper, mode_upper,
                i < machine.mode_count - 1 ? "," : "");
    }
    fprintf(out, "} %s_mode_t;\n\n", prefix);

    /* Context struct */
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    %s_mode_t current;\n", prefix);
    fprintf(out, "    %s_mode_t previous;\n", prefix);
    fprintf(out, "} %s_ctx_t;\n\n", prefix);

    /* Functions */
    fprintf(out, "void %s_init(%s_ctx_t *ctx);\n", prefix, prefix);
    fprintf(out, "void %s_dispatch(%s_ctx_t *ctx, int event);\n", prefix, prefix);
    fprintf(out, "const char *%s_mode_name(%s_mode_t mode);\n\n", prefix, prefix);

    fprintf(out, "#endif /* %s_MSM_H */\n", upper);
    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_msm_c(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s_msm.c", outdir, prefix);

    FILE *out = fopen(path, "w");
    if (!out) return -1;

    char upper[MAX_NAME];
    strncpy(upper, prefix, MAX_NAME - 1);
    to_upper(upper);

    time_t now = time(NULL);
    fprintf(out, "/* AUTO-GENERATED by msmgen %s — DO NOT EDIT\n", MSMGEN_VERSION);
    fprintf(out, " * @generated %s", ctime(&now));
    fprintf(out, " * Regenerate: make regen\n */\n\n");
    fprintf(out, "#include \"%s_msm.h\"\n\n", prefix);

    /* Mode names */
    fprintf(out, "static const char *mode_names[] = {\n");
    for (int i = 0; i < machine.mode_count; i++) {
        fprintf(out, "    \"%s\"%s\n", machine.modes[i].name,
                i < machine.mode_count - 1 ? "," : "");
    }
    fprintf(out, "};\n\n");

    fprintf(out, "const char *%s_mode_name(%s_mode_t mode) {\n", prefix, prefix);
    fprintf(out, "    return mode_names[mode];\n}\n\n");

    /* Init */
    char def_upper[MAX_NAME];
    strncpy(def_upper, machine.default_mode, MAX_NAME - 1);
    to_upper(def_upper);

    fprintf(out, "void %s_init(%s_ctx_t *ctx) {\n", prefix, prefix);
    fprintf(out, "    ctx->current = %s_MODE_%s;\n", upper, def_upper);
    fprintf(out, "    ctx->previous = ctx->current;\n");
    fprintf(out, "}\n\n");

    /* Dispatch */
    fprintf(out, "void %s_dispatch(%s_ctx_t *ctx, int event) {\n", prefix, prefix);
    fprintf(out, "    %s_mode_t next = ctx->current;\n", prefix);
    fprintf(out, "    switch (ctx->current) {\n");

    for (int i = 0; i < machine.mode_count; i++) {
        msm_mode_t *m = &machine.modes[i];
        char mode_upper[MAX_NAME];
        strncpy(mode_upper, m->name, MAX_NAME - 1);
        to_upper(mode_upper);

        fprintf(out, "    case %s_MODE_%s:\n", upper, mode_upper);
        fprintf(out, "        switch (event) {\n");
        for (int j = 0; j < m->trans_count; j++) {
            char tgt_upper[MAX_NAME];
            strncpy(tgt_upper, m->trans[j].target, MAX_NAME - 1);
            to_upper(tgt_upper);
            fprintf(out, "        case '%s': next = %s_MODE_%s; break;\n",
                    m->trans[j].event, upper, tgt_upper);
        }
        fprintf(out, "        default: break;\n");
        fprintf(out, "        }\n        break;\n");
    }

    fprintf(out, "    }\n");
    fprintf(out, "    if (next != ctx->current) {\n");
    fprintf(out, "        ctx->previous = ctx->current;\n");
    fprintf(out, "        ctx->current = next;\n");
    fprintf(out, "    }\n}\n");

    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static void print_usage(void) {
    fprintf(stderr, "msmgen %s — Modal State Machine Generator\n", MSMGEN_VERSION);
    fprintf(stderr, "Usage: msmgen <input.msm> [output_dir] [prefix]\n");
}

int main(int argc, char *argv[]) {
    tool_harden();
    if (argc < 2) { print_usage(); return 1; }

    const char *input = argv[1];
    const char *outdir = argc > 2 ? argv[2] : ".";

    char prefix[MAX_NAME];
    const char *basename = strrchr(input, '/');
    basename = basename ? basename + 1 : input;
    strncpy(prefix, basename, MAX_NAME - 1);
    char *dot = strchr(prefix, '.');
    if (dot) *dot = '\0';

    if (argc > 3) strncpy(prefix, argv[3], MAX_NAME - 1);

    if (parse_msm(input) != 0) return 1;

    fprintf(stderr, "Parsed modal machine '%s' with %d modes\n",
            machine.name, machine.mode_count);

    if (ensure_output_dir(outdir) != 0 && errno != EEXIST) return 1;

    if (generate_msm_h(outdir, prefix) != 0) return 1;
    if (generate_msm_c(outdir, prefix) != 0) return 1;

    return 0;
}
