/* cosmo-bde — Business Rules Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates rule evaluation functions from .rules specs.
 *
 * Usage: clipsgen <input.rules> [output_dir] [prefix]
 *
 * Input:
 *   rule ApplyDiscount {
 *       when { order.total > 100; customer.tier == "gold" }
 *       then { order.discount = 0.15 }
 *   }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#include "clipsgen_self.h"

#define CLIPSGEN_VERSION "1.1.0"
#define MAX_PATH 512
#define MAX_NAME 64
#define MAX_LINE 1024
#define MAX_CONDS 16
#define MAX_ACTIONS 16
#define MAX_RULES 64
#define MAX_FIELDS 64

typedef struct { char expr[MAX_LINE]; } condition_t;
typedef struct { char stmt[MAX_LINE]; } action_t;

typedef struct {
    char name[MAX_NAME];
    condition_t conditions[MAX_CONDS];
    int cond_count;
    action_t actions[MAX_ACTIONS];
    int action_count;
    int priority;
} rule_t;

typedef enum {
    FIELD_INT = 0,
    FIELD_DOUBLE = 1,
    FIELD_STRING = 2
} field_kind_t;

typedef struct {
    char name[MAX_NAME];
    field_kind_t kind;
} ctx_field_t;

static rule_t rules[MAX_RULES];
static int rule_count = 0;
static ctx_field_t ctx_fields[MAX_FIELDS];
static int ctx_field_count = 0;

static void trim(char *s) {
    char *start = s;
    size_t len;

    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);

    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static void to_upper(char *s) { for (; *s; s++) *s = toupper((unsigned char)*s); }

static int has_decimal_literal(const char *s) {
    const char *p = s;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.' && isdigit((unsigned char)p[1])) return 1;
        } else {
            p++;
        }
    }
    return 0;
}

static field_kind_t infer_field_kind(const char *field_name, const char *expr) {
    char lower[MAX_NAME];
    size_t i;

    if (strchr(expr, '"') || strchr(expr, '\'')) return FIELD_STRING;
    if (has_decimal_literal(expr)) return FIELD_DOUBLE;

    strncpy(lower, field_name, MAX_NAME - 1);
    lower[MAX_NAME - 1] = '\0';
    for (i = 0; lower[i]; i++) lower[i] = (char)tolower((unsigned char)lower[i]);

    if (strstr(lower, "total") || strstr(lower, "amount") ||
        strstr(lower, "price") || strstr(lower, "cost") ||
        strstr(lower, "discount") || strstr(lower, "rate") ||
        strstr(lower, "ratio")) {
        return FIELD_DOUBLE;
    }

    return FIELD_INT;
}

static void register_ctx_field(const char *field_name, field_kind_t kind) {
    int i;

    for (i = 0; i < ctx_field_count; i++) {
        if (strcmp(ctx_fields[i].name, field_name) == 0) {
            if (kind > ctx_fields[i].kind) ctx_fields[i].kind = kind;
            return;
        }
    }

    if (ctx_field_count >= MAX_FIELDS) return;

    strncpy(ctx_fields[ctx_field_count].name, field_name, MAX_NAME - 1);
    ctx_fields[ctx_field_count].name[MAX_NAME - 1] = '\0';
    ctx_fields[ctx_field_count].kind = kind;
    ctx_field_count++;
}

static void collect_ctx_fields(const char *expr) {
    const char *p = expr;

    while ((p = strstr(p, "ctx->")) != NULL) {
        char name[MAX_NAME];
        size_t len = 0;
        field_kind_t kind;

        p += 5;
        while ((isalnum((unsigned char)p[len]) || p[len] == '_') && len < MAX_NAME - 1) {
            name[len] = p[len];
            len++;
        }
        name[len] = '\0';
        if (len == 0) continue;

        kind = infer_field_kind(name, expr);
        register_ctx_field(name, kind);
        p += len;
    }
}

static int parse_rules(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { fprintf(stderr, "Error: Cannot open %s\n", filename); return -1; }

    char line[MAX_LINE];
    rule_t *current = NULL;
    int in_when = 0, in_then = 0;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        if (strncmp(line, "rule ", 5) == 0) {
            char *n = line + 5;
            char *b = strchr(n, '{');
            if (b) *b = '\0';
            trim(n);
            current = &rules[rule_count++];
            memset(current, 0, sizeof(*current));
            strncpy(current->name, n, MAX_NAME - 1);
            current->name[MAX_NAME - 1] = '\0';
            in_when = 0;
            in_then = 0;
            continue;
        }

        if (strncmp(line, "when", 4) == 0) { in_when = 1; in_then = 0; continue; }
        if (strncmp(line, "then", 4) == 0) { in_when = 0; in_then = 1; continue; }
        if (strcmp(line, "{") == 0) continue;
        if (line[0] == '}') {
            if (in_when) {
                in_when = 0;
            } else if (in_then) {
                in_then = 0;
            } else {
                current = NULL;
            }
            continue;
        }

        if (current && in_when && line[0] != '{') {
            char *semi = strchr(line, ';');
            if (semi) *semi = '\0';
            trim(line);
            if (line[0]) {
                strncpy(current->conditions[current->cond_count++].expr, line, MAX_LINE - 1);
                current->conditions[current->cond_count - 1].expr[MAX_LINE - 1] = '\0';
                collect_ctx_fields(line);
            }
        }

        if (current && in_then && line[0] != '{') {
            char *semi = strchr(line, ';');
            if (semi) *semi = '\0';
            trim(line);
            if (line[0]) {
                strncpy(current->actions[current->action_count++].stmt, line, MAX_LINE - 1);
                current->actions[current->action_count - 1].stmt[MAX_LINE - 1] = '\0';
                collect_ctx_fields(line);
            }
        }
    }

    fclose(f);
    return 0;
}

static int generate_rules_h(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s_rules.h", outdir, prefix);

    FILE *out = fopen(path, "w");
    if (!out) return -1;

    char upper[MAX_NAME];
    strncpy(upper, prefix, MAX_NAME - 1);
    upper[MAX_NAME - 1] = '\0';
    to_upper(upper);

    time_t now = time(NULL);
    fprintf(out, "/* AUTO-GENERATED by clipsgen %s */\n", CLIPSGEN_VERSION);
    fprintf(out, "/* @generated %s */\n\n", ctime(&now));
    fprintf(out, "#ifndef %s_RULES_H\n#define %s_RULES_H\n\n", upper, upper);

    fprintf(out, "/* Rule IDs */\n");
    fprintf(out, "typedef enum {\n");
    for (int i = 0; i < rule_count; i++) {
        char rule_upper[MAX_NAME];
        strncpy(rule_upper, rules[i].name, MAX_NAME - 1);
        to_upper(rule_upper);
        fprintf(out, "    %s_RULE_%s%s\n", upper, rule_upper,
                i < rule_count - 1 ? "," : "");
    }
    fprintf(out, "} %s_rule_id_t;\n\n", prefix);

    /* Generate inferred context struct (can be overridden) */
    fprintf(out, "/* Context struct - define %s_CTX_DEFINED before including to use your own */\n", upper);
    fprintf(out, "#ifndef %s_CTX_DEFINED\n", upper);
    fprintf(out, "typedef struct %s_ctx {\n", prefix);
    if (ctx_field_count == 0) {
        fprintf(out, "    int _unused;\n");
    } else {
        for (int i = 0; i < ctx_field_count; i++) {
            const char *ctype = "int";
            if (ctx_fields[i].kind == FIELD_DOUBLE) ctype = "double";
            if (ctx_fields[i].kind == FIELD_STRING) ctype = "char";

            if (ctx_fields[i].kind == FIELD_STRING) {
                fprintf(out, "    %s %s[128];\n", ctype, ctx_fields[i].name);
            } else {
                fprintf(out, "    %s %s;\n", ctype, ctx_fields[i].name);
            }
        }
    }
    fprintf(out, "} %s_ctx_t;\n", prefix);
    fprintf(out, "#else\n");
    fprintf(out, "typedef struct %s_ctx %s_ctx_t;\n", prefix, prefix);
    fprintf(out, "#endif\n\n");

    fprintf(out, "/* Rule evaluation */\n");
    fprintf(out, "int %s_evaluate_all(%s_ctx_t *ctx);\n", prefix, prefix);
    for (int i = 0; i < rule_count; i++) {
        fprintf(out, "int %s_rule_%s(%s_ctx_t *ctx);\n", prefix, rules[i].name, prefix);
    }

    fprintf(out, "\n#endif /* %s_RULES_H */\n", upper);
    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_rules_c(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s_rules.c", outdir, prefix);

    FILE *out = fopen(path, "w");
    if (!out) return -1;

    time_t now = time(NULL);
    fprintf(out, "/* AUTO-GENERATED by clipsgen %s */\n", CLIPSGEN_VERSION);
    fprintf(out, "/* @generated %s */\n\n", ctime(&now));
    fprintf(out, "#include \"%s_rules.h\"\n\n", prefix);

    /* Generate each rule function */
    for (int i = 0; i < rule_count; i++) {
        rule_t *r = &rules[i];
        fprintf(out, "/* Rule: %s */\n", r->name);
        fprintf(out, "int %s_rule_%s(%s_ctx_t *ctx) {\n", prefix, r->name, prefix);
        fprintf(out, "    /* Conditions (AND-ed) */\n");
        fprintf(out, "    if (!(1");
        for (int j = 0; j < r->cond_count; j++) {
            fprintf(out, "\n        && (%s)", r->conditions[j].expr);
        }
        fprintf(out, ")) return 0;\n\n");
        fprintf(out, "    /* Actions */\n");
        for (int j = 0; j < r->action_count; j++) {
            fprintf(out, "    %s;\n", r->actions[j].stmt);
        }
        fprintf(out, "    return 1;\n}\n\n");
    }

    /* Evaluate all */
    fprintf(out, "int %s_evaluate_all(%s_ctx_t *ctx) {\n", prefix, prefix);
    fprintf(out, "    int fired = 0;\n");
    for (int i = 0; i < rule_count; i++) {
        fprintf(out, "    fired += %s_rule_%s(ctx);\n", prefix, rules[i].name);
    }
    fprintf(out, "    return fired;\n}\n");

    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static void print_usage(void) {
    fprintf(stderr, "clipsgen %s — Business Rules Generator\n", CLIPSGEN_VERSION);
    fprintf(stderr, "Usage: clipsgen <input.rules> [output_dir] [prefix]\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    const char *input = argv[1];
    const char *outdir = argc > 2 ? argv[2] : ".";

    char prefix[MAX_NAME];
    const char *basename = strrchr(input, '/');
    basename = basename ? basename + 1 : input;
    strncpy(prefix, basename, MAX_NAME - 1);
    prefix[MAX_NAME - 1] = '\0';
    char *dot = strchr(prefix, '.');
    if (dot) *dot = '\0';

    if (argc > 3) {
        strncpy(prefix, argv[3], MAX_NAME - 1);
        prefix[MAX_NAME - 1] = '\0';
    }

    if (parse_rules(input) != 0) return 1;

    fprintf(stderr, "Parsed %d rules from %s\n", rule_count, input);

    struct stat st;
    if (stat(outdir, &st) != 0) {
#ifdef _WIN32
        mkdir(outdir);
#else
        mkdir(outdir, 0755);
#endif
    }

    if (generate_rules_h(outdir, prefix) != 0) return 1;
    if (generate_rules_c(outdir, prefix) != 0) return 1;

    return 0;
}
