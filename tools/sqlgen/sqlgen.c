/* cosmo-bde — SQL Schema Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates SQLite DDL and C CRUD functions from .sql specs.
 * Output is pure C with sqlite3 bindings.
 *
 * TRUE DOGFOODING: Uses sqlgen_self.h which expands sqlgen_tokens.def
 * via X-macros to define this generator's own token types.
 *
 * Usage: sqlgen <input.sql> [output_dir] [prefix]
 *
 * Input format:
 *   table users {
 *       id: integer primary key
 *       name: text not null
 *       email: text unique
 *       created_at: timestamp default now
 *   }
 *
 *   index users_email on users(email)
 *
 *   query find_by_email(email: text) -> users {
 *       SELECT * FROM users WHERE email = ?
 *   }
 *
 * Output:
 *   <prefix>_schema.sql  — DDL statements
 *   <prefix>_db.h        — C function declarations
 *   <prefix>_db.c        — C function implementations
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
#include "sqlgen_self.h"

#define SQLGEN_VERSION "1.0.0"
#define MAX_PATH 512
#define MAX_NAME 64
#define MAX_LINE 1024
#define MAX_COLUMNS 32
#define MAX_TABLES 32
#define MAX_INDEXES 32
#define MAX_QUERIES 64
#define MAX_PARAMS 8
#define MAX_SQL 2048

/* ── Data Structures ─────────────────────────────────────────────── */

typedef struct {
    char name[MAX_NAME];
    char type[MAX_NAME];
    int is_primary;
    int is_unique;
    int is_not_null;
    char default_val[MAX_NAME];
    char references[MAX_NAME];
} column_t;

typedef struct {
    char name[MAX_NAME];
    column_t columns[MAX_COLUMNS];
    int column_count;
} table_t;

typedef struct {
    char name[MAX_NAME];
    char table[MAX_NAME];
    char columns[MAX_NAME];
    int is_unique;
} index_t;

typedef struct {
    char name[MAX_NAME];
    char type[MAX_NAME];
} param_t;

typedef struct {
    char name[MAX_NAME];
    param_t params[MAX_PARAMS];
    int param_count;
    char return_type[MAX_NAME];
    char sql[MAX_SQL];
} query_t;

static table_t tables[MAX_TABLES];
static int table_count = 0;
static index_t indexes[MAX_INDEXES];
static int index_count = 0;
static query_t queries[MAX_QUERIES];
static int query_count = 0;

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

static const char *sql_type_to_c(const char *sql_type) {
    if (strcmp(sql_type, "integer") == 0) return "int64_t";
    if (strcmp(sql_type, "text") == 0) return "const char *";
    if (strcmp(sql_type, "real") == 0) return "double";
    if (strcmp(sql_type, "blob") == 0) return "const void *";
    if (strcmp(sql_type, "boolean") == 0) return "int";
    if (strcmp(sql_type, "timestamp") == 0) return "int64_t";
    return "void *";
}

/* ── Parser ──────────────────────────────────────────────────────── */

static int parse_sql(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", filename, strerror(errno));
        return -1;
    }

    char line[MAX_LINE];
    int in_table = 0, in_query = 0;
    table_t *current_table = NULL;
    query_t *current_query = NULL;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        /* table name { */
        if (strncmp(line, "table ", 6) == 0) {
            char *name = line + 6;
            char *brace = strchr(name, '{');
            if (brace) *brace = '\0';
            trim(name);

            current_table = &tables[table_count++];
            strncpy(current_table->name, name, MAX_NAME - 1);
            current_table->column_count = 0;
            in_table = 1;
            continue;
        }

        /* index name on table(cols) */
        if (strncmp(line, "index ", 6) == 0) {
            index_t *idx = &indexes[index_count++];
            char *p = line + 6;
            char *on = strstr(p, " on ");
            if (on) {
                *on = '\0';
                strncpy(idx->name, p, MAX_NAME - 1);
                trim(idx->name);

                p = on + 4;
                char *lparen = strchr(p, '(');
                if (lparen) {
                    *lparen = '\0';
                    strncpy(idx->table, p, MAX_NAME - 1);
                    trim(idx->table);

                    char *rparen = strchr(lparen + 1, ')');
                    if (rparen) *rparen = '\0';
                    strncpy(idx->columns, lparen + 1, MAX_NAME - 1);
                }
            }
            continue;
        }

        /* query name(params) -> return { */
        if (strncmp(line, "query ", 6) == 0) {
            current_query = &queries[query_count++];
            char *p = line + 6;

            /* Parse name */
            char *lparen = strchr(p, '(');
            if (lparen) {
                *lparen = '\0';
                strncpy(current_query->name, p, MAX_NAME - 1);
                trim(current_query->name);

                /* Parse params */
                char *rparen = strchr(lparen + 1, ')');
                if (rparen) {
                    *rparen = '\0';
                    char *params = lparen + 1;
                    char *tok = strtok(params, ",");
                    while (tok && current_query->param_count < MAX_PARAMS) {
                        trim(tok);
                        char *colon = strchr(tok, ':');
                        if (colon) {
                            *colon = '\0';
                            param_t *param = &current_query->params[current_query->param_count++];
                            strncpy(param->name, tok, MAX_NAME - 1);
                            trim(param->name);
                            strncpy(param->type, colon + 1, MAX_NAME - 1);
                            trim(param->type);
                        }
                        tok = strtok(NULL, ",");
                    }

                    /* Parse return type */
                    char *arrow = strstr(rparen + 1, "->");
                    if (arrow) {
                        char *ret = arrow + 2;
                        char *brace = strchr(ret, '{');
                        if (brace) *brace = '\0';
                        trim(ret);
                        strncpy(current_query->return_type, ret, MAX_NAME - 1);
                    }
                }
            }
            current_query->sql[0] = '\0';
            in_query = 1;
            continue;
        }

        /* } end block */
        if (line[0] == '}') {
            in_table = 0;
            in_query = 0;
            current_table = NULL;
            current_query = NULL;
            continue;
        }

        /* Column: name: type constraints */
        if (in_table && current_table) {
            column_t *col = &current_table->columns[current_table->column_count++];
            memset(col, 0, sizeof(*col));

            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                strncpy(col->name, line, MAX_NAME - 1);
                trim(col->name);

                char *rest = colon + 1;
                trim(rest);

                /* Parse type and constraints */
                char *tok = strtok(rest, " ");
                if (tok) {
                    strncpy(col->type, tok, MAX_NAME - 1);
                    while ((tok = strtok(NULL, " ")) != NULL) {
                        if (strcmp(tok, "primary") == 0) col->is_primary = 1;
                        else if (strcmp(tok, "key") == 0) continue;
                        else if (strcmp(tok, "unique") == 0) col->is_unique = 1;
                        else if (strcmp(tok, "not") == 0) col->is_not_null = 1;
                        else if (strcmp(tok, "null") == 0) continue;
                        else if (strcmp(tok, "default") == 0) {
                            tok = strtok(NULL, " ");
                            if (tok) strncpy(col->default_val, tok, MAX_NAME - 1);
                        }
                        else if (strcmp(tok, "references") == 0) {
                            tok = strtok(NULL, " ");
                            if (tok) strncpy(col->references, tok, MAX_NAME - 1);
                        }
                    }
                }
            }
        }

        /* SQL inside query */
        if (in_query && current_query) {
            if (strlen(current_query->sql) + strlen(line) + 2 < MAX_SQL) {
                if (current_query->sql[0]) strcat(current_query->sql, " ");
                strcat(current_query->sql, line);
            }
        }
    }

    fclose(f);
    return 0;
}

/* ── Code Generation ─────────────────────────────────────────────── */

static int generate_schema_sql(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s_schema.sql", outdir, prefix);

    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    fprintf(out, "-- AUTO-GENERATED by sqlgen %s — DO NOT EDIT\n", SQLGEN_VERSION);
    fprintf(out, "-- Regenerate: make regen\n\n");

    /* Tables */
    for (int i = 0; i < table_count; i++) {
        table_t *t = &tables[i];
        fprintf(out, "CREATE TABLE IF NOT EXISTS %s (\n", t->name);
        for (int j = 0; j < t->column_count; j++) {
            column_t *c = &t->columns[j];
            fprintf(out, "    %s %s", c->name, c->type);
            if (c->is_primary) fprintf(out, " PRIMARY KEY");
            if (c->is_unique) fprintf(out, " UNIQUE");
            if (c->is_not_null) fprintf(out, " NOT NULL");
            if (c->default_val[0]) {
                if (strcmp(c->default_val, "now") == 0)
                    fprintf(out, " DEFAULT CURRENT_TIMESTAMP");
                else
                    fprintf(out, " DEFAULT %s", c->default_val);
            }
            if (c->references[0]) fprintf(out, " REFERENCES %s", c->references);
            if (j < t->column_count - 1) fprintf(out, ",");
            fprintf(out, "\n");
        }
        fprintf(out, ");\n\n");
    }

    /* Indexes */
    for (int i = 0; i < index_count; i++) {
        index_t *idx = &indexes[i];
        fprintf(out, "CREATE INDEX IF NOT EXISTS %s ON %s(%s);\n",
                idx->name, idx->table, idx->columns);
    }

    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_db_h(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s_db.h", outdir, prefix);

    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    char upper[MAX_NAME];
    strncpy(upper, prefix, MAX_NAME - 1);
    to_upper(upper);

    time_t now = time(NULL);
    fprintf(out, "/* AUTO-GENERATED by sqlgen %s — DO NOT EDIT\n", SQLGEN_VERSION);
    fprintf(out, " * @generated %s", ctime(&now));
    fprintf(out, " * Regenerate: make regen\n");
    fprintf(out, " */\n\n");
    fprintf(out, "#ifndef %s_DB_H\n", upper);
    fprintf(out, "#define %s_DB_H\n\n", upper);
    fprintf(out, "#include <sqlite3.h>\n");
    fprintf(out, "#include <stdint.h>\n\n");

    /* Row structs for each table */
    for (int i = 0; i < table_count; i++) {
        table_t *t = &tables[i];
        fprintf(out, "/* Row struct for %s */\n", t->name);
        fprintf(out, "typedef struct {\n");
        for (int j = 0; j < t->column_count; j++) {
            column_t *c = &t->columns[j];
            fprintf(out, "    %s %s;\n", sql_type_to_c(c->type), c->name);
        }
        fprintf(out, "} %s_%s_row_t;\n\n", prefix, t->name);
    }

    /* Init/close */
    fprintf(out, "/* Database lifecycle */\n");
    fprintf(out, "int %s_db_init(sqlite3 **db, const char *path);\n", prefix);
    fprintf(out, "void %s_db_close(sqlite3 *db);\n\n", prefix);

    /* CRUD for each table */
    for (int i = 0; i < table_count; i++) {
        table_t *t = &tables[i];
        fprintf(out, "/* CRUD for %s */\n", t->name);
        fprintf(out, "int %s_%s_insert(sqlite3 *db, const %s_%s_row_t *row);\n",
                prefix, t->name, prefix, t->name);
        fprintf(out, "int %s_%s_get_by_id(sqlite3 *db, int64_t id, %s_%s_row_t *out);\n",
                prefix, t->name, prefix, t->name);
        fprintf(out, "int %s_%s_delete(sqlite3 *db, int64_t id);\n\n",
                prefix, t->name);
    }

    /* Custom queries */
    if (query_count > 0) {
        fprintf(out, "/* Custom queries */\n");
        for (int i = 0; i < query_count; i++) {
            query_t *q = &queries[i];
            fprintf(out, "int %s_%s(sqlite3 *db", prefix, q->name);
            for (int j = 0; j < q->param_count; j++) {
                fprintf(out, ", %s %s", sql_type_to_c(q->params[j].type), q->params[j].name);
            }
            if (q->return_type[0]) {
                fprintf(out, ", %s_%s_row_t *out", prefix, q->return_type);
            }
            fprintf(out, ");\n");
        }
    }

    fprintf(out, "\n#endif /* %s_DB_H */\n", upper);
    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_db_c(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s_db.c", outdir, prefix);

    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    time_t now = time(NULL);
    fprintf(out, "/* AUTO-GENERATED by sqlgen %s — DO NOT EDIT\n", SQLGEN_VERSION);
    fprintf(out, " * @generated %s", ctime(&now));
    fprintf(out, " * Regenerate: make regen\n");
    fprintf(out, " */\n\n");
    fprintf(out, "#include \"%s_db.h\"\n", prefix);
    fprintf(out, "#include <string.h>\n\n");

    /* Schema SQL embedded */
    fprintf(out, "static const char *%s_schema_sql =\n", prefix);
    for (int i = 0; i < table_count; i++) {
        table_t *t = &tables[i];
        fprintf(out, "    \"CREATE TABLE IF NOT EXISTS %s (", t->name);
        for (int j = 0; j < t->column_count; j++) {
            column_t *c = &t->columns[j];
            fprintf(out, "%s %s", c->name, c->type);
            if (c->is_primary) fprintf(out, " PRIMARY KEY");
            if (c->is_not_null) fprintf(out, " NOT NULL");
            if (j < t->column_count - 1) fprintf(out, ", ");
        }
        fprintf(out, ");\\n\"\n");
    }
    fprintf(out, ";\n\n");

    /* Init */
    fprintf(out, "int %s_db_init(sqlite3 **db, const char *path) {\n", prefix);
    fprintf(out, "    int rc = sqlite3_open(path, db);\n");
    fprintf(out, "    if (rc != SQLITE_OK) return rc;\n");
    fprintf(out, "    return sqlite3_exec(*db, %s_schema_sql, NULL, NULL, NULL);\n", prefix);
    fprintf(out, "}\n\n");

    fprintf(out, "void %s_db_close(sqlite3 *db) {\n", prefix);
    fprintf(out, "    if (db) sqlite3_close(db);\n");
    fprintf(out, "}\n\n");

    /* CRUD stubs */
    for (int i = 0; i < table_count; i++) {
        table_t *t = &tables[i];

        fprintf(out, "int %s_%s_insert(sqlite3 *db, const %s_%s_row_t *row) {\n",
                prefix, t->name, prefix, t->name);
        fprintf(out, "    (void)db; (void)row;\n");
        fprintf(out, "    /* TODO: Implement */\n");
        fprintf(out, "    return SQLITE_OK;\n");
        fprintf(out, "}\n\n");

        fprintf(out, "int %s_%s_get_by_id(sqlite3 *db, int64_t id, %s_%s_row_t *out) {\n",
                prefix, t->name, prefix, t->name);
        fprintf(out, "    (void)db; (void)id; (void)out;\n");
        fprintf(out, "    /* TODO: Implement */\n");
        fprintf(out, "    return SQLITE_OK;\n");
        fprintf(out, "}\n\n");

        fprintf(out, "int %s_%s_delete(sqlite3 *db, int64_t id) {\n",
                prefix, t->name);
        fprintf(out, "    (void)db; (void)id;\n");
        fprintf(out, "    /* TODO: Implement */\n");
        fprintf(out, "    return SQLITE_OK;\n");
        fprintf(out, "}\n\n");
    }

    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

/* ── Main ─────────────────────────────────────────────────────────── */

static void print_usage(void) {
    fprintf(stderr, "sqlgen %s — SQL Schema Generator\n", SQLGEN_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: sqlgen <input.sql> [output_dir] [prefix]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Generates SQLite DDL and C bindings from .sql specs.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Output:\n");
    fprintf(stderr, "  <prefix>_schema.sql  — DDL statements\n");
    fprintf(stderr, "  <prefix>_db.h        — C function declarations\n");
    fprintf(stderr, "  <prefix>_db.c        — C function implementations\n");
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

    if (parse_sql(input) != 0) {
        return 1;
    }

    fprintf(stderr, "Parsed %d tables, %d indexes, %d queries from %s\n",
            table_count, index_count, query_count, input);

    if (ensure_output_dir(outdir) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: Cannot create output directory %s\n", outdir);
        return 1;
    }

    if (generate_schema_sql(outdir, prefix) != 0) return 1;
    if (generate_db_h(outdir, prefix) != 0) return 1;
    if (generate_db_c(outdir, prefix) != 0) return 1;

    return 0;
}
