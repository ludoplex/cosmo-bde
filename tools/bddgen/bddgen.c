/* MBSE Stacks — BDD Test Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates C test harness code from Gherkin .feature files.
 * Output is pure C with no runtime dependencies.
 *
 * TRUE DOGFOODING: Uses bddgen_self.h which expands bddgen_tokens.def
 * via X-macros to define this generator's own token types.
 *
 * Usage: bddgen <feature.feature> [output_dir] [prefix]
 *
 * Generates:
 *   <prefix>_bdd.h    - Test declarations, step function prototypes
 *   <prefix>_bdd.c    - Test harness, step matcher, runner
 *   <prefix>_steps.c  - Step skeleton implementations (if not exists)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

/* ── Self-hosted tokens (dogfooding) ─────────────────────────────── */
#include "bddgen_self.h"

#define BDDGEN_VERSION "1.0.0"
#define MAX_LINE 1024
#define MAX_FEATURES 16
#define MAX_SCENARIOS 128
#define MAX_STEPS 512
#define MAX_NAME 256
#define MAX_TEXT 512
#define MAX_PATH 512
#define MAX_TAGS 32
#define MAX_TAG_LEN 64

/* ── Data Structures ─────────────────────────────────────────────── */

typedef enum {
    STEP_GIVEN = 0,
    STEP_WHEN = 1,
    STEP_THEN = 2,
    STEP_AND = 3,
    STEP_BUT = 4
} step_keyword_t;

typedef struct {
    step_keyword_t keyword;
    step_keyword_t resolved_keyword;  /* AND/BUT resolved to GIVEN/WHEN/THEN */
    char text[MAX_TEXT];
    int line_number;
    int scenario_index;
} step_t;

typedef struct {
    char name[MAX_NAME];
    int step_start;
    int step_count;
    int line_number;
    int is_outline;
    char tags[MAX_TAGS][MAX_TAG_LEN];
    int tag_count;
} scenario_t;

typedef struct {
    char name[MAX_NAME];
    char description[MAX_TEXT];
    scenario_t scenarios[MAX_SCENARIOS];
    int scenario_count;
    int background_step_start;
    int background_step_count;
    char tags[MAX_TAGS][MAX_TAG_LEN];
    int tag_count;
    int line_number;
} feature_t;

static feature_t features[MAX_FEATURES];
static int feature_count = 0;
static step_t steps[MAX_STEPS];
static int step_count = 0;

/* ── Utilities ────────────────────────────────────────────────────── */

static void trim(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
}

/* Escape quotes and backslashes for C string literals */
static void escape_c_string(const char *src, char *dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 1; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j < dst_size - 2) {
                dst[j++] = '\\';
                dst[j++] = c;
            }
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

static void to_snake_case(const char *src, char *dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 1; i++) {
        char c = src[i];
        if (isalnum((unsigned char)c)) {
            dst[j++] = (char)tolower((unsigned char)c);
        } else if (c == ' ' || c == '-' || c == '_') {
            if (j > 0 && dst[j-1] != '_') {
                dst[j++] = '_';
            }
        }
    }
    dst[j] = '\0';
}

static void to_upper(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static const char *step_keyword_str(step_keyword_t kw) {
    switch (kw) {
        case STEP_GIVEN: return "Given";
        case STEP_WHEN:  return "When";
        case STEP_THEN:  return "Then";
        case STEP_AND:   return "And";
        case STEP_BUT:   return "But";
    }
    return "Unknown";
}

/* ── Parser ───────────────────────────────────────────────────────── */

static step_keyword_t parse_step_keyword(const char *word) {
    if (strcmp(word, "Given") == 0) return STEP_GIVEN;
    if (strcmp(word, "When") == 0) return STEP_WHEN;
    if (strcmp(word, "Then") == 0) return STEP_THEN;
    if (strcmp(word, "And") == 0) return STEP_AND;
    if (strcmp(word, "But") == 0) return STEP_BUT;
    return STEP_GIVEN;
}

static int parse_step(const char *line, int line_num, step_keyword_t *last_keyword) {
    if (step_count >= MAX_STEPS) {
        fprintf(stderr, "Error: Too many steps\n");
        return -1;
    }

    step_t *s = &steps[step_count];
    memset(s, 0, sizeof(*s));
    s->line_number = line_num;

    /* Parse keyword */
    char keyword[32];
    const char *p = line;
    int i = 0;
    while (*p && isalpha((unsigned char)*p) && i < 31) {
        keyword[i++] = *p++;
    }
    keyword[i] = '\0';

    s->keyword = parse_step_keyword(keyword);

    /* Resolve AND/BUT to previous actual keyword */
    if (s->keyword == STEP_AND || s->keyword == STEP_BUT) {
        s->resolved_keyword = *last_keyword;
    } else {
        s->resolved_keyword = s->keyword;
        *last_keyword = s->keyword;
    }

    /* Skip whitespace and get step text */
    while (*p && isspace((unsigned char)*p)) p++;
    strncpy(s->text, p, MAX_TEXT - 1);
    trim(s->text);

    step_count++;
    return 0;
}

static void parse_tags(const char *line, char tags[][MAX_TAG_LEN], int *tag_count) {
    const char *p = line;
    while (*p && *tag_count < MAX_TAGS) {
        if (*p == '@') {
            p++;
            int i = 0;
            while (*p && !isspace((unsigned char)*p) && i < MAX_TAG_LEN - 1) {
                tags[*tag_count][i++] = *p++;
            }
            tags[*tag_count][i] = '\0';
            (*tag_count)++;
        } else {
            p++;
        }
    }
}

static int parse_feature(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", filename);
        return -1;
    }

    char line[MAX_LINE];
    int line_num = 0;
    feature_t *cur_feature = NULL;
    scenario_t *cur_scenario = NULL;
    step_keyword_t last_keyword = STEP_GIVEN;
    int in_background = 0;
    char pending_tags[MAX_TAGS][MAX_TAG_LEN];
    int pending_tag_count = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;
        trim(line);

        /* Skip empty lines and comments */
        if (line[0] == '\0') continue;
        if (line[0] == '#') continue;

        /* Tags */
        if (line[0] == '@') {
            parse_tags(line, pending_tags, &pending_tag_count);
            continue;
        }

        /* Feature: */
        if (strncmp(line, "Feature:", 8) == 0) {
            if (feature_count >= MAX_FEATURES) {
                fprintf(stderr, "Error: Too many features\n");
                fclose(f);
                return -1;
            }
            cur_feature = &features[feature_count++];
            memset(cur_feature, 0, sizeof(*cur_feature));
            cur_feature->line_number = line_num;
            cur_feature->background_step_start = -1;

            /* Copy tags */
            for (int t = 0; t < pending_tag_count; t++) {
                strncpy(cur_feature->tags[t], pending_tags[t], MAX_TAG_LEN - 1);
            }
            cur_feature->tag_count = pending_tag_count;
            pending_tag_count = 0;

            char *name = line + 8;
            while (*name && isspace((unsigned char)*name)) name++;
            strncpy(cur_feature->name, name, MAX_NAME - 1);
            continue;
        }

        /* Background: */
        if (strncmp(line, "Background:", 11) == 0 && cur_feature) {
            in_background = 1;
            cur_feature->background_step_start = step_count;
            cur_scenario = NULL;
            continue;
        }

        /* Scenario: or Scenario Outline: */
        if ((strncmp(line, "Scenario:", 9) == 0 || 
             strncmp(line, "Scenario Outline:", 17) == 0) && cur_feature) {
            in_background = 0;
            if (cur_feature->scenario_count >= MAX_SCENARIOS) {
                fprintf(stderr, "Error: Too many scenarios\n");
                fclose(f);
                return -1;
            }
            cur_scenario = &cur_feature->scenarios[cur_feature->scenario_count++];
            memset(cur_scenario, 0, sizeof(*cur_scenario));
            cur_scenario->line_number = line_num;
            cur_scenario->step_start = step_count;
            cur_scenario->is_outline = (strncmp(line, "Scenario Outline:", 17) == 0);

            /* Copy tags */
            for (int t = 0; t < pending_tag_count; t++) {
                strncpy(cur_scenario->tags[t], pending_tags[t], MAX_TAG_LEN - 1);
            }
            cur_scenario->tag_count = pending_tag_count;
            pending_tag_count = 0;

            char *name = line + (cur_scenario->is_outline ? 17 : 9);
            while (*name && isspace((unsigned char)*name)) name++;
            strncpy(cur_scenario->name, name, MAX_NAME - 1);
            continue;
        }

        /* Step keywords */
        if (strncmp(line, "Given ", 6) == 0 || strncmp(line, "When ", 5) == 0 ||
            strncmp(line, "Then ", 5) == 0 || strncmp(line, "And ", 4) == 0 ||
            strncmp(line, "But ", 4) == 0) {

            if (in_background && cur_feature) {
                parse_step(line, line_num, &last_keyword);
                cur_feature->background_step_count++;
            } else if (cur_scenario) {
                parse_step(line, line_num, &last_keyword);
                steps[step_count - 1].scenario_index = cur_feature->scenario_count - 1;
                cur_scenario->step_count++;
            }
            continue;
        }

        /* Description text (after Feature:) */
        if (cur_feature && cur_feature->description[0] == '\0' && cur_feature->scenario_count == 0) {
            /* Multi-line description - skip for now */
        }
    }

    fclose(f);
    return 0;
}

/* ── Code Generation ──────────────────────────────────────────────── */

static void generate_header_guard(FILE *out, const char *guard) {
    fprintf(out, "/* AUTO-GENERATED by bddgen %s — DO NOT EDIT */\n", BDDGEN_VERSION);
    fprintf(out, "#ifndef %s\n", guard);
    fprintf(out, "#define %s\n\n", guard);
}

static int generate_bdd_h(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char guard[MAX_NAME];
    char header_name[MAX_NAME];
    char lower_prefix[64];

    strncpy(lower_prefix, prefix, sizeof(lower_prefix) - 1);
    lower_prefix[sizeof(lower_prefix) - 1] = '\0';
    for (char *p = lower_prefix; *p; p++) *p = (char)tolower((unsigned char)*p);

    snprintf(header_name, sizeof(header_name), "%s_bdd.h", lower_prefix);
    snprintf(guard, sizeof(guard), "%s_BDD_H", prefix);
    to_upper(guard);

    snprintf(path, sizeof(path), "%s/%s", outdir, header_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    generate_header_guard(out, guard);

    fprintf(out, "#include <stdbool.h>\n");
    fprintf(out, "#include <stddef.h>\n\n");

    /* Test result enum */
    fprintf(out, "/* Test result status */\n");
    fprintf(out, "typedef enum {\n");
    fprintf(out, "    %s_PASS = 0,\n", prefix);
    fprintf(out, "    %s_FAIL,\n", prefix);
    fprintf(out, "    %s_SKIP,\n", prefix);
    fprintf(out, "    %s_PENDING,\n", prefix);
    fprintf(out, "    %s_ERROR\n", prefix);
    fprintf(out, "} %s_result_t;\n\n", prefix);

    /* Step context */
    fprintf(out, "/* Step execution context */\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    void *world;           /* User-defined world state */\n");
    fprintf(out, "    const char *step_text; /* Current step text */\n");
    fprintf(out, "    int step_line;         /* Line number in feature file */\n");
    fprintf(out, "    const char *scenario;  /* Current scenario name */\n");
    fprintf(out, "    const char *feature;   /* Current feature name */\n");
    fprintf(out, "} %s_context_t;\n\n", prefix);

    /* Test statistics */
    fprintf(out, "/* Test run statistics */\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    int total_scenarios;\n");
    fprintf(out, "    int passed_scenarios;\n");
    fprintf(out, "    int failed_scenarios;\n");
    fprintf(out, "    int skipped_scenarios;\n");
    fprintf(out, "    int total_steps;\n");
    fprintf(out, "    int passed_steps;\n");
    fprintf(out, "    int failed_steps;\n");
    fprintf(out, "} %s_stats_t;\n\n", prefix);

    /* Step function prototype */
    fprintf(out, "/* Step function prototype */\n");
    fprintf(out, "typedef %s_result_t (*%s_step_fn)(%s_context_t *ctx);\n\n", prefix, prefix, prefix);

    /* Runner functions */
    fprintf(out, "/* Test runner */\n");
    fprintf(out, "void %s_run_all(void *world, %s_stats_t *stats);\n", prefix, prefix);
    fprintf(out, "void %s_run_scenario(void *world, int scenario_index, %s_stats_t *stats);\n", prefix, prefix);
    fprintf(out, "void %s_print_stats(const %s_stats_t *stats);\n\n", prefix, prefix);

    /* Step definition prototypes (user implements) */
    fprintf(out, "/* Step definitions (implement these in %s_steps.c) */\n", lower_prefix);
    
    /* Track unique steps */
    char unique_steps[MAX_STEPS][MAX_TEXT];
    int unique_count = 0;
    
    for (int i = 0; i < step_count; i++) {
        int found = 0;
        for (int j = 0; j < unique_count; j++) {
            if (strcmp(unique_steps[j], steps[i].text) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && unique_count < MAX_STEPS) {
            strncpy(unique_steps[unique_count++], steps[i].text, MAX_TEXT - 1);
            
            char func_name[MAX_NAME];
            to_snake_case(steps[i].text, func_name, sizeof(func_name));
            fprintf(out, "%s_result_t step_%s(%s_context_t *ctx);\n", prefix, func_name, prefix);
        }
    }

    fprintf(out, "\n#endif /* %s */\n", guard);
    fclose(out);

    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_bdd_c(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char header_name[MAX_NAME];
    char impl_name[MAX_NAME];
    char lower_prefix[64];

    strncpy(lower_prefix, prefix, sizeof(lower_prefix) - 1);
    lower_prefix[sizeof(lower_prefix) - 1] = '\0';
    for (char *p = lower_prefix; *p; p++) *p = (char)tolower((unsigned char)*p);

    snprintf(header_name, sizeof(header_name), "%s_bdd.h", lower_prefix);
    snprintf(impl_name, sizeof(impl_name), "%s_bdd.c", lower_prefix);

    snprintf(path, sizeof(path), "%s/%s", outdir, impl_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    fprintf(out, "/* AUTO-GENERATED by bddgen %s — DO NOT EDIT */\n\n", BDDGEN_VERSION);
    fprintf(out, "#include \"%s\"\n", header_name);
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <string.h>\n\n");

    /* Feature names */
    fprintf(out, "/* Feature data */\n");
    fprintf(out, "static const char *feature_names[] = {\n");
    for (int fi = 0; fi < feature_count; fi++) {
        feature_t *f = &features[fi];
        fprintf(out, "    \"%s\",\n", f->name);
    }
    fprintf(out, "};\n");
    fprintf(out, "static const int generated_feature_count = %d;\n\n", feature_count);

    /* Scenario data */
    fprintf(out, "/* Scenario data */\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    const char *name;\n");
    fprintf(out, "    int background_step_start;\n");
    fprintf(out, "    int background_step_count;\n");
    fprintf(out, "    int step_start;\n");
    fprintf(out, "    int step_count;\n");
    fprintf(out, "    int feature_index;\n");
    fprintf(out, "} scenario_info_t;\n\n");

    fprintf(out, "static const scenario_info_t scenarios[] = {\n");
    int generated_scenario_count = 0;
    for (int fi = 0; fi < feature_count; fi++) {
        feature_t *f = &features[fi];
        for (int si = 0; si < f->scenario_count; si++) {
            scenario_t *s = &f->scenarios[si];
            fprintf(out, "    {\"%s\", %d, %d, %d, %d, %d},\n",
                    s->name,
                    f->background_step_start,
                    f->background_step_count,
                    s->step_start,
                    s->step_count,
                    fi);
            generated_scenario_count++;
        }
    }
    fprintf(out, "};\n");
    fprintf(out, "static const int scenario_count = %d;\n\n", generated_scenario_count);

    /* Step data */
    fprintf(out, "/* Step data */\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    const char *text;\n");
    fprintf(out, "    int keyword;  /* 0=Given, 1=When, 2=Then */\n");
    fprintf(out, "    int line_number;\n");
    fprintf(out, "    %s_step_fn function;\n", prefix);
    fprintf(out, "} step_info_t;\n\n");

    /* Generate step function references */
    fprintf(out, "static const step_info_t steps[] = {\n");
    for (int i = 0; i < step_count; i++) {
        char func_name[MAX_NAME];
        char escaped_text[MAX_TEXT * 2];
        to_snake_case(steps[i].text, func_name, sizeof(func_name));
        escape_c_string(steps[i].text, escaped_text, sizeof(escaped_text));
        fprintf(out, "    {\"%s\", %d, %d, step_%s},\n",
                escaped_text, steps[i].resolved_keyword,
                steps[i].line_number, func_name);
    }
    fprintf(out, "};\n");
    fprintf(out, "static const int total_steps = %d;\n\n", step_count);

    /* Result names */
    fprintf(out, "static const char *result_names[] = {\n");
    fprintf(out, "    \"PASS\", \"FAIL\", \"SKIP\", \"PENDING\", \"ERROR\"\n");
    fprintf(out, "};\n\n");

    /* Run single scenario */
    fprintf(out, "void %s_run_scenario(void *world, int scenario_index, %s_stats_t *stats) {\n", prefix, prefix);
    fprintf(out, "    if (scenario_index < 0 || scenario_index >= scenario_count) return;\n\n");
    fprintf(out, "    const scenario_info_t *sc = &scenarios[scenario_index];\n");
    fprintf(out, "    const char *feature_name = \"\";\n");
    fprintf(out, "    if (sc->feature_index >= 0 && sc->feature_index < generated_feature_count)\n");
    fprintf(out, "        feature_name = feature_names[sc->feature_index];\n\n");
    fprintf(out, "    printf(\"  Scenario: %%s\\n\", sc->name);\n\n");
    fprintf(out, "    %s_context_t ctx;\n", prefix);
    fprintf(out, "    ctx.world = world;\n");
    fprintf(out, "    ctx.scenario = sc->name;\n");
    fprintf(out, "    ctx.feature = feature_name;\n\n");
    fprintf(out, "    int scenario_passed = 1;\n");
    fprintf(out, "    for (int i = 0; i < sc->background_step_count; i++) {\n");
    fprintf(out, "        int step_idx = sc->background_step_start + i;\n");
    fprintf(out, "        if (step_idx < 0 || step_idx >= total_steps) break;\n\n");
    fprintf(out, "        const step_info_t *st = &steps[step_idx];\n");
    fprintf(out, "        ctx.step_text = st->text;\n");
    fprintf(out, "        ctx.step_line = st->line_number;\n\n");
    fprintf(out, "        const char *keyword = (st->keyword == 0) ? \"Given\" :\n");
    fprintf(out, "                              (st->keyword == 1) ? \"When\" : \"Then\";\n");
    fprintf(out, "        printf(\"    %%s %%s... \", keyword, st->text);\n\n");
    fprintf(out, "        %s_result_t result = %s_PENDING;\n", prefix, prefix);
    fprintf(out, "        if (st->function) {\n");
    fprintf(out, "            result = st->function(&ctx);\n");
    fprintf(out, "        }\n\n");
    fprintf(out, "        printf(\"%%s\\n\", result_names[result]);\n");
    fprintf(out, "        stats->total_steps++;\n\n");
    fprintf(out, "        if (result == %s_PASS) {\n", prefix);
    fprintf(out, "            stats->passed_steps++;\n");
    fprintf(out, "        } else {\n");
    fprintf(out, "            stats->failed_steps++;\n");
    fprintf(out, "            scenario_passed = 0;\n");
    fprintf(out, "        }\n");
    fprintf(out, "    }\n\n");
    fprintf(out, "    for (int i = 0; i < sc->step_count; i++) {\n");
    fprintf(out, "        int step_idx = sc->step_start + i;\n");
    fprintf(out, "        if (step_idx >= total_steps) break;\n\n");
    fprintf(out, "        const step_info_t *st = &steps[step_idx];\n");
    fprintf(out, "        ctx.step_text = st->text;\n");
    fprintf(out, "        ctx.step_line = st->line_number;\n\n");
    fprintf(out, "        const char *keyword = (st->keyword == 0) ? \"Given\" :\n");
    fprintf(out, "                              (st->keyword == 1) ? \"When\" : \"Then\";\n");
    fprintf(out, "        printf(\"    %%s %%s... \", keyword, st->text);\n\n");
    fprintf(out, "        %s_result_t result = %s_PENDING;\n", prefix, prefix);
    fprintf(out, "        if (st->function) {\n");
    fprintf(out, "            result = st->function(&ctx);\n");
    fprintf(out, "        }\n\n");
    fprintf(out, "        printf(\"%%s\\n\", result_names[result]);\n");
    fprintf(out, "        stats->total_steps++;\n\n");
    fprintf(out, "        if (result == %s_PASS) {\n", prefix);
    fprintf(out, "            stats->passed_steps++;\n");
    fprintf(out, "        } else {\n");
    fprintf(out, "            stats->failed_steps++;\n");
    fprintf(out, "            scenario_passed = 0;\n");
    fprintf(out, "        }\n");
    fprintf(out, "    }\n\n");
    fprintf(out, "    stats->total_scenarios++;\n");
    fprintf(out, "    if (scenario_passed) {\n");
    fprintf(out, "        stats->passed_scenarios++;\n");
    fprintf(out, "    } else {\n");
    fprintf(out, "        stats->failed_scenarios++;\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");

    /* Run all scenarios */
    fprintf(out, "void %s_run_all(void *world, %s_stats_t *stats) {\n", prefix, prefix);
    fprintf(out, "    memset(stats, 0, sizeof(*stats));\n\n");
    fprintf(out, "    int last_feature_index = -1;\n");
    fprintf(out, "    for (int i = 0; i < scenario_count; i++) {\n");
    fprintf(out, "        const scenario_info_t *sc = &scenarios[i];\n");
    fprintf(out, "        if (sc->feature_index != last_feature_index) {\n");
    fprintf(out, "            if (i > 0) printf(\"\\n\");\n");
    fprintf(out, "            printf(\"Feature: %%s\\n\\n\", feature_names[sc->feature_index]);\n");
    fprintf(out, "            last_feature_index = sc->feature_index;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        %s_run_scenario(world, i, stats);\n", prefix);
    fprintf(out, "        printf(\"\\n\");\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");

    /* Print stats */
    fprintf(out, "void %s_print_stats(const %s_stats_t *stats) {\n", prefix, prefix);
    fprintf(out, "    printf(\"\\n═══════════════════════════════════════════════════════════\\n\");\n");
    fprintf(out, "    printf(\"BDD Test Results:\\n\");\n");
    fprintf(out, "    printf(\"  Scenarios: %%d total, %%d passed, %%d failed, %%d skipped\\n\",\n");
    fprintf(out, "           stats->total_scenarios, stats->passed_scenarios,\n");
    fprintf(out, "           stats->failed_scenarios, stats->skipped_scenarios);\n");
    fprintf(out, "    printf(\"  Steps:     %%d total, %%d passed, %%d failed\\n\",\n");
    fprintf(out, "           stats->total_steps, stats->passed_steps, stats->failed_steps);\n");
    fprintf(out, "    printf(\"═══════════════════════════════════════════════════════════\\n\");\n");
    fprintf(out, "}\n");

    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_steps_skeleton(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char header_name[MAX_NAME];
    char steps_name[MAX_NAME];
    char lower_prefix[64];

    strncpy(lower_prefix, prefix, sizeof(lower_prefix) - 1);
    lower_prefix[sizeof(lower_prefix) - 1] = '\0';
    for (char *p = lower_prefix; *p; p++) *p = (char)tolower((unsigned char)*p);

    snprintf(header_name, sizeof(header_name), "%s_bdd.h", lower_prefix);
    snprintf(steps_name, sizeof(steps_name), "%s_steps.c", lower_prefix);
    snprintf(path, sizeof(path), "%s/%s", outdir, steps_name);

    /* Don't overwrite existing steps file */
    FILE *check = fopen(path, "r");
    if (check) {
        fclose(check);
        fprintf(stderr, "Skipped %s (already exists)\n", path);
        return 0;
    }

    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    fprintf(out, "/* Step definitions for BDD tests\n");
    fprintf(out, " * Generated skeleton by bddgen %s\n", BDDGEN_VERSION);
    fprintf(out, " * Implement each step function to make tests pass.\n");
    fprintf(out, " */\n\n");
    fprintf(out, "#include \"%s\"\n", header_name);
    fprintf(out, "#include <stdio.h>\n\n");

    /* Track unique steps */
    char unique_steps[MAX_STEPS][MAX_TEXT];
    int unique_count = 0;

    for (int i = 0; i < step_count; i++) {
        int found = 0;
        for (int j = 0; j < unique_count; j++) {
            if (strcmp(unique_steps[j], steps[i].text) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && unique_count < MAX_STEPS) {
            strncpy(unique_steps[unique_count++], steps[i].text, MAX_TEXT - 1);

            char func_name[MAX_NAME];
            to_snake_case(steps[i].text, func_name, sizeof(func_name));

            fprintf(out, "/* %s %s */\n", step_keyword_str(steps[i].resolved_keyword), steps[i].text);
            fprintf(out, "%s_result_t step_%s(%s_context_t *ctx) {\n", prefix, func_name, prefix);
            fprintf(out, "    (void)ctx; /* TODO: implement */\n");
            fprintf(out, "    return %s_PENDING;\n", prefix);
            fprintf(out, "}\n\n");
        }
    }

    fclose(out);
    fprintf(stderr, "Generated %s (skeleton)\n", path);
    return 0;
}

static void generate_version(const char *outdir, const char *profile) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/GENERATOR_VERSION", outdir);

    FILE *out = fopen(path, "w");
    if (!out) return;

    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    fprintf(out, "bddgen %s\n", BDDGEN_VERSION);
    fprintf(out, "generated: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
    fprintf(out, "profile: %s\n", profile);
    fprintf(out, "features: %d\n", feature_count);
    int total_scenarios = 0;
    for (int i = 0; i < feature_count; i++) {
        total_scenarios += features[i].scenario_count;
    }
    fprintf(out, "scenarios: %d\n", total_scenarios);
    fprintf(out, "steps: %d\n", step_count);

    fclose(out);
}

static int ensure_output_dir(const char *outdir) {
    if (!outdir || !*outdir) return 0;
    if (mkdir(outdir, 0777) == 0 || errno == EEXIST) return 0;
    perror("mkdir");
    return -1;
}

/* ── Main ─────────────────────────────────────────────────────────── */

static void print_usage(void) {
    fprintf(stderr, "bddgen %s — BDD Test Generator\n", BDDGEN_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: bddgen <feature.feature> [output_dir] [prefix]\n");
    fprintf(stderr, "       bddgen --run <feature.feature> [more.feature ...]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Parses Gherkin .feature files and generates C test harness.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --run    Parse and report only, do not generate files\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Output:\n");
    fprintf(stderr, "  <prefix>_bdd.h    — Test declarations, step prototypes\n");
    fprintf(stderr, "  <prefix>_bdd.c    — Test harness and runner\n");
    fprintf(stderr, "  <prefix>_steps.c  — Step skeleton (if not exists)\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    /* --run mode: parse and report only, no file generation */
    if (strcmp(argv[1], "--run") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: --run requires at least one .feature file\n");
            return 1;
        }
        int total_features = 0, total_scenarios = 0, total_steps = 0;
        for (int i = 2; i < argc; i++) {
            /* Reset state for each file */
            feature_count = 0;
            step_count = 0;

            if (parse_feature(argv[i]) != 0) {
                fprintf(stderr, "Error: Failed to parse %s\n", argv[i]);
                continue;
            }
            for (int f = 0; f < feature_count; f++) {
                printf("Feature: %s (%d scenario(s), %d background step(s))\n",
                       features[f].name, features[f].scenario_count,
                       features[f].background_step_count);
                for (int s = 0; s < features[f].scenario_count; s++) {
                    printf("  Scenario: %s (%d step(s))\n",
                           features[f].scenarios[s].name,
                           features[f].scenarios[s].step_count);
                }
                total_scenarios += features[f].scenario_count;
            }
            total_features += feature_count;
            total_steps += step_count;
        }
        printf("\nTotal: %d feature(s), %d scenario(s), %d step(s)\n",
               total_features, total_scenarios, total_steps);
        return 0;
    }

    const char *input = argv[1];
    const char *outdir = argc > 2 ? argv[2] : ".";
    const char *profile = getenv("PROFILE");
    if (!profile) profile = "portable";

    /* Derive prefix from filename */
    char prefix[MAX_NAME];
    const char *basename = strrchr(input, '/');
    basename = basename ? basename + 1 : input;
    strncpy(prefix, basename, MAX_NAME - 1);
    char *dot = strchr(prefix, '.');
    if (dot) *dot = '\0';
    to_upper(prefix);

    if (argc > 3) {
        strncpy(prefix, argv[3], MAX_NAME - 1);
    }

    if (parse_feature(input) != 0) {
        return 1;
    }

    int total_scenarios = 0;
    for (int i = 0; i < feature_count; i++) {
        total_scenarios += features[i].scenario_count;
    }

    fprintf(stderr, "Parsed %d features, %d scenarios, %d steps from %s\n",
            feature_count, total_scenarios, step_count, input);

    if (ensure_output_dir(outdir) != 0) {
        return 1;
    }

    if (generate_bdd_h(outdir, prefix) != 0) return 1;
    if (generate_bdd_c(outdir, prefix) != 0) return 1;
    if (generate_steps_skeleton(outdir, prefix) != 0) return 1;

    generate_version(outdir, profile);

    return 0;
}
