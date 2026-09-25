/* MBSE Stacks — Definition File Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates C code from X-macro .def files.
 * Uses the cosmopolitan pattern: .def files are directly #include-able.
 *
 * TRUE DOGFOODING: Uses defgen_self.h which expands defgen_macros.def
 * via X-macros to define the recognized macro patterns.
 *
 * Usage: defgen <input.def> [output_dir] [prefix]
 *
 * The .def file format uses X-macros that expand based on context:
 *   TABLE(name, doc)
 *   FIELD(table, name, c_type, sql_type, flags, doc)
 *   TABLE_END(name)
 *
 * Or token format:
 *   TOK(name, lexeme, kind, doc)
 *
 * Or state machine format:
 *   SM_STATE(machine, name, entry, exit, doc)
 *   SM_TRANS(machine, source, event, target, guard, action, doc)
 */

#include <stdio.h>
#include "../cosmo_harden.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

/* ── Self-hosted macros (dogfooding) ─────────────────────────────── */
#include "defgen_self.h"

#define DEFGEN_VERSION "1.0.0"
#define MAX_PATH 512
#define MAX_NAME 64

/* ── X-Macro Expansion Templates ─────────────────────────────────── */

/* Template: Generate enum from TOK() macros */
static void emit_token_enum(FILE *out, const char *def_path, const char *prefix) {
    fprintf(out, "/* Token enum generated from %s */\n", def_path);
    fprintf(out, "typedef enum {\n");
    fprintf(out, "    %s_TOK_EOF = 0,\n", prefix);
    fprintf(out, "    %s_TOK_ERROR,\n", prefix);
    fprintf(out, "#define TOK(name, lexeme, kind, doc) %s_TOK_##name,\n", prefix);
    fprintf(out, "#include \"%s\"\n", def_path);
    fprintf(out, "#undef TOK\n");
    fprintf(out, "    %s_TOK_COUNT\n", prefix);
    fprintf(out, "} %s_token_t;\n\n", prefix);
}

/* Template: Generate token name array from TOK() macros */
static void emit_token_names(FILE *out, const char *def_path, const char *prefix) {
    fprintf(out, "/* Token names array */\n");
    fprintf(out, "static const char *%s_token_names[] = {\n", prefix);
    fprintf(out, "    \"EOF\",\n");
    fprintf(out, "    \"ERROR\",\n");
    fprintf(out, "#define TOK(name, lexeme, kind, doc) #name,\n");
    fprintf(out, "#include \"%s\"\n", def_path);
    fprintf(out, "#undef TOK\n");
    fprintf(out, "};\n\n");
}

/* Template: Generate keyword table from TOK() macros */
static void emit_keyword_table(FILE *out, const char *def_path, const char *prefix) {
    fprintf(out, "/* Keyword table (keywords only) */\n");
    fprintf(out, "typedef struct { const char *kw; %s_token_t tok; } %s_kw_t;\n", prefix, prefix);
    fprintf(out, "static const %s_kw_t %s_keywords[] = {\n", prefix, prefix);
    fprintf(out, "#define TOK(name, lexeme, kind, doc) \\\n");
    fprintf(out, "    { (strcmp(#kind, \"keyword\") == 0) ? lexeme : NULL, %s_TOK_##name },\n", prefix);
    fprintf(out, "#include \"%s\"\n", def_path);
    fprintf(out, "#undef TOK\n");
    fprintf(out, "    { NULL, 0 }\n");
    fprintf(out, "};\n\n");
}

/* Template: Generate struct from TABLE/FIELD macros */
static void emit_table_structs(FILE *out, const char *def_path, const char *prefix) {
    fprintf(out, "/* Struct definitions generated from %s */\n\n", def_path);

    /* Forward declarations */
    fprintf(out, "/* Forward declarations */\n");
    fprintf(out, "#define TABLE(name, doc) typedef struct name name##_t;\n");
    fprintf(out, "#define FIELD(tbl, name, ctype, sql, flags, doc)\n");
    fprintf(out, "#define TABLE_END(name)\n");
    fprintf(out, "#include \"%s\"\n", def_path);
    fprintf(out, "#undef TABLE\n");
    fprintf(out, "#undef FIELD\n");
    fprintf(out, "#undef TABLE_END\n\n");

    /* Struct definitions */
    fprintf(out, "/* Struct definitions */\n");
    fprintf(out, "#define TABLE(name, doc) struct name { /* doc */\n");
    fprintf(out, "#define FIELD(tbl, name, ctype, sql, flags, doc) ctype name; /* doc */\n");
    fprintf(out, "#define TABLE_END(name) };\n");
    fprintf(out, "#include \"%s\"\n", def_path);
    fprintf(out, "#undef TABLE\n");
    fprintf(out, "#undef FIELD\n");
    fprintf(out, "#undef TABLE_END\n\n");
}

/* Template: Generate SQL CREATE statements */
static void emit_sql_create(FILE *out, const char *def_path, const char *prefix) {
    (void)prefix;
    fprintf(out, "/* SQL CREATE macros generated from %s */\n\n", def_path);

    fprintf(out, "/* Use like: printf(SQL_CREATE_TableName); */\n");
    fprintf(out, "#define TABLE(name, doc) \\\n");
    fprintf(out, "    static const char SQL_CREATE_##name[] = \"CREATE TABLE \" #name \" (\"\n");
    fprintf(out, "#define FIELD(tbl, name, ctype, sqltype, flags, doc) \\\n");
    fprintf(out, "    #name \" \" #sqltype\n");
    fprintf(out, "#define TABLE_END(name) \\\n");
    fprintf(out, "    \")\";\n");
    fprintf(out, "/* Note: Above is a simplified template. Real SQL gen needs field separator logic. */\n\n");
}

/* Template: Generate state enum from SM_STATE macros */
static void emit_sm_states(FILE *out, const char *def_path, const char *prefix, const char *machine) {
    fprintf(out, "/* State enum for %s */\n", machine);
    fprintf(out, "typedef enum {\n");
    fprintf(out, "#define SM_STATE(mach, name, entry, exit, doc) \\\n");
    fprintf(out, "    ((strcmp(#mach, \"%s\") == 0) ? %s_STATE_##name : -1),\n", machine, prefix);
    fprintf(out, "#define SM_TRANS(mach, src, evt, tgt, guard, act, doc)\n");
    fprintf(out, "#include \"%s\"\n", def_path);
    fprintf(out, "#undef SM_STATE\n");
    fprintf(out, "#undef SM_TRANS\n");
    fprintf(out, "    %s_STATE_COUNT\n", prefix);
    fprintf(out, "} %s_state_t;\n\n", prefix);
}

/* ── Main ─────────────────────────────────────────────────────────── */

static void print_usage(void) {
    fprintf(stderr, "defgen %s — X-Macro Definition File Processor\n", DEFGEN_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: defgen <input.def> [output_dir] [prefix]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Processes .def files using X-macro expansion.\n");
    fprintf(stderr, "The .def file is directly #include-able in generated code.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Supported formats:\n");
    fprintf(stderr, "  TOK(name, lexeme, kind, doc)              Token definitions\n");
    fprintf(stderr, "  TABLE(name, doc) / FIELD(...) / TABLE_END Schema definitions\n");
    fprintf(stderr, "  SM_STATE / SM_TRANS                       State machines\n");
}

static int ensure_output_dir(const char *outdir) {
    if (!outdir || !*outdir) return 0;
    if (mkdir(outdir, 0777) == 0 || errno == EEXIST) return 0;
    perror("mkdir");
    return -1;
}

int main(int argc, char *argv[]) {
    tool_harden();
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *input = argv[1];
    const char *outdir = argc > 2 ? argv[2] : ".";
    const char *prefix = argc > 3 ? argv[3] : "MBSE";
    const char *profile = getenv("PROFILE");
    if (!profile) profile = "portable";

    /* Determine def file type by scanning for markers */
    FILE *f = fopen(input, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", input);
        return 1;
    }

    int has_tok = 0, has_table = 0, has_sm = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "TOK(")) has_tok = 1;
        if (strstr(line, "TABLE(")) has_table = 1;
        if (strstr(line, "SM_STATE(")) has_sm = 1;
    }
    fclose(f);

    if (ensure_output_dir(outdir) != 0) {
        return 1;
    }

    /* Extract just the filename for #include */
    const char *basename = strrchr(input, '/');
    basename = basename ? basename + 1 : input;

    char path[MAX_PATH];
    char lower_prefix[MAX_NAME];
    strncpy(lower_prefix, prefix, MAX_NAME - 1);
    for (char *p = lower_prefix; *p; p++) *p = (char)tolower((unsigned char)*p);

    /* Generate appropriate output based on def type */
    if (has_tok) {
        snprintf(path, sizeof(path), "%s/%s_tokens.h", outdir, lower_prefix);
        FILE *out = fopen(path, "w");
        if (!out) { perror("fopen"); return 1; }

        fprintf(out, "/* AUTO-GENERATED by defgen %s — DO NOT EDIT */\n", DEFGEN_VERSION);
        fprintf(out, "#ifndef %s_TOKENS_H\n", prefix);
        fprintf(out, "#define %s_TOKENS_H\n\n", prefix);
        fprintf(out, "#include <string.h>\n\n");

        emit_token_enum(out, basename, prefix);
        emit_token_names(out, basename, prefix);
        emit_keyword_table(out, basename, prefix);

        fprintf(out, "#endif /* %s_TOKENS_H */\n", prefix);
        fclose(out);
        fprintf(stderr, "Generated %s (tokens)\n", path);
    }

    if (has_table) {
        snprintf(path, sizeof(path), "%s/%s_model.h", outdir, lower_prefix);
        FILE *out = fopen(path, "w");
        if (!out) { perror("fopen"); return 1; }

        fprintf(out, "/* AUTO-GENERATED by defgen %s — DO NOT EDIT */\n", DEFGEN_VERSION);
        fprintf(out, "#ifndef %s_MODEL_H\n", prefix);
        fprintf(out, "#define %s_MODEL_H\n\n", prefix);
        fprintf(out, "#include <stdint.h>\n\n");

        emit_table_structs(out, basename, prefix);

        fprintf(out, "#endif /* %s_MODEL_H */\n", prefix);
        fclose(out);
        fprintf(stderr, "Generated %s (model)\n", path);

        /* Also generate SQL header */
        snprintf(path, sizeof(path), "%s/%s_sql.h", outdir, lower_prefix);
        out = fopen(path, "w");
        if (!out) { perror("fopen"); return 1; }

        fprintf(out, "/* AUTO-GENERATED by defgen %s — DO NOT EDIT */\n", DEFGEN_VERSION);
        fprintf(out, "#ifndef %s_SQL_H\n", prefix);
        fprintf(out, "#define %s_SQL_H\n\n", prefix);

        emit_sql_create(out, basename, prefix);

        fprintf(out, "#endif /* %s_SQL_H */\n", prefix);
        fclose(out);
        fprintf(stderr, "Generated %s (sql)\n", path);
    }

    if (has_sm) {
        snprintf(path, sizeof(path), "%s/%s_sm.h", outdir, lower_prefix);
        FILE *out = fopen(path, "w");
        if (!out) { perror("fopen"); return 1; }

        fprintf(out, "/* AUTO-GENERATED by defgen %s — DO NOT EDIT */\n", DEFGEN_VERSION);
        fprintf(out, "#ifndef %s_SM_H\n", prefix);
        fprintf(out, "#define %s_SM_H\n\n", prefix);

        /* Note: Would need to parse machine names from file */
        emit_sm_states(out, basename, prefix, "GenSM");

        fprintf(out, "#endif /* %s_SM_H */\n", prefix);
        fclose(out);
        fprintf(stderr, "Generated %s (state machine)\n", path);
    }

    /* Generate version stamp */
    snprintf(path, sizeof(path), "%s/GENERATOR_VERSION", outdir);
    FILE *out = fopen(path, "w");
    if (out) {
        time_t now = time(NULL);
        struct tm *t = gmtime(&now);
        fprintf(out, "defgen %s\n", DEFGEN_VERSION);
        fprintf(out, "generated: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
                t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                t->tm_hour, t->tm_min, t->tm_sec);
        fprintf(out, "profile: %s\n", profile);
        fclose(out);
    }

    return 0;
}
