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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ── Self-hosted tokens (dogfooding) ─────────────────────────────── */
#include "sqlgen_self.h"

#define SQLGEN_VERSION "1.1.0"
#define MAX_PATH 512
#define MAX_NAME 64
#define MAX_LINE 1024
#define MAX_COLUMNS 32
#define MAX_TABLES 32
#define MAX_INDEXES 32
#define MAX_QUERIES 64
#define MAX_PARAMS 8
#define MAX_SQL 4096

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
    size_t len;

    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);

    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static void to_upper(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static int ensure_output_dir(const char *outdir) {
    struct stat st;
    if (stat(outdir, &st) == 0) return 0;
#ifdef _WIN32
    return mkdir(outdir);
#else
    return mkdir(outdir, 0755);
#endif
}

static int appendf(char *buf, size_t size, size_t *len, const char *fmt, ...) {
    va_list ap;
    int written;

    if (*len >= size) return -1;

    va_start(ap, fmt);
    written = vsnprintf(buf + *len, size - *len, fmt, ap);
    va_end(ap);

    if (written < 0) return -1;
    if (*len + (size_t)written >= size) return -1;

    *len += (size_t)written;
    return 0;
}

static void emit_c_string_literal(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; ++s) {
        switch (*s) {
        case '\\':
            fputs("\\\\", out);
            break;
        case '"':
            fputs("\\\"", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            fputc(*s, out);
            break;
        }
    }
    fputc('"', out);
}

static int is_sql_real_type(const char *sql_type) {
    return strcmp(sql_type, "real") == 0;
}

static int is_sql_text_type(const char *sql_type) {
    return strcmp(sql_type, "text") == 0;
}

static int is_sql_timestamp_type(const char *sql_type) {
    return strcmp(sql_type, "timestamp") == 0;
}

static int is_sql_blob_type(const char *sql_type) {
    return strcmp(sql_type, "blob") == 0;
}

static const char *sql_type_to_c_param(const char *sql_type) {
    if (strcmp(sql_type, "integer") == 0) return "int64_t";
    if (strcmp(sql_type, "text") == 0) return "const char *";
    if (strcmp(sql_type, "real") == 0) return "double";
    if (strcmp(sql_type, "blob") == 0) return "const void *";
    if (strcmp(sql_type, "boolean") == 0) return "int";
    if (strcmp(sql_type, "timestamp") == 0) return "const char *";
    return "const char *";
}

static const char *sql_type_to_c_field(const char *sql_type) {
    if (strcmp(sql_type, "integer") == 0) return "int64_t";
    if (strcmp(sql_type, "text") == 0) return "char *";
    if (strcmp(sql_type, "real") == 0) return "double";
    if (strcmp(sql_type, "blob") == 0) return "void *";
    if (strcmp(sql_type, "boolean") == 0) return "int";
    if (strcmp(sql_type, "timestamp") == 0) return "char *";
    return "char *";
}

static table_t *find_table_by_name(const char *name) {
    int i;
    for (i = 0; i < table_count; i++) {
        if (strcmp(tables[i].name, name) == 0) {
            return &tables[i];
        }
    }
    return NULL;
}

static int find_primary_key_column(const table_t *table) {
    int i;

    for (i = 0; i < table->column_count; i++) {
        if (table->columns[i].is_primary) return i;
    }
    for (i = 0; i < table->column_count; i++) {
        if (strcmp(table->columns[i].name, "id") == 0) return i;
    }
    return table->column_count > 0 ? 0 : -1;
}

static int build_create_table_sql(const table_t *table, char *buf, size_t size) {
    size_t len = 0;
    int i;

    if (appendf(buf, size, &len, "CREATE TABLE IF NOT EXISTS %s (", table->name) != 0) {
        return -1;
    }

    for (i = 0; i < table->column_count; i++) {
        const column_t *col = &table->columns[i];

        if (appendf(buf, size, &len, "%s %s", col->name, col->type) != 0) return -1;
        if (col->is_primary && appendf(buf, size, &len, " PRIMARY KEY") != 0) return -1;
        if (col->is_unique && appendf(buf, size, &len, " UNIQUE") != 0) return -1;
        if (col->is_not_null && appendf(buf, size, &len, " NOT NULL") != 0) return -1;
        if (col->default_val[0]) {
            if (strcmp(col->default_val, "now") == 0) {
                if (appendf(buf, size, &len, " DEFAULT CURRENT_TIMESTAMP") != 0) return -1;
            } else {
                if (appendf(buf, size, &len, " DEFAULT %s", col->default_val) != 0) return -1;
            }
        }
        if (col->references[0] &&
            appendf(buf, size, &len, " REFERENCES %s", col->references) != 0) {
            return -1;
        }
        if (i < table->column_count - 1 && appendf(buf, size, &len, ", ") != 0) return -1;
    }

    if (appendf(buf, size, &len, ");\n") != 0) return -1;
    return 0;
}

static int build_index_sql(const index_t *idx, char *buf, size_t size) {
    size_t len = 0;

    if (appendf(buf, size, &len, "CREATE %sINDEX IF NOT EXISTS %s ON %s(%s);\n",
                idx->is_unique ? "UNIQUE " : "",
                idx->name,
                idx->table,
                idx->columns) != 0) {
        return -1;
    }

    return 0;
}

static int build_insert_sql(const table_t *table, char *buf, size_t size) {
    size_t len = 0;
    int i;

    if (appendf(buf, size, &len, "INSERT INTO %s (", table->name) != 0) return -1;
    for (i = 0; i < table->column_count; i++) {
        if (appendf(buf, size, &len, "%s%s",
                    table->columns[i].name,
                    i < table->column_count - 1 ? ", " : "") != 0) {
            return -1;
        }
    }
    if (appendf(buf, size, &len, ") VALUES (") != 0) return -1;

    for (i = 0; i < table->column_count; i++) {
        const column_t *col = &table->columns[i];

        if (col->default_val[0]) {
            if (strcmp(col->default_val, "now") == 0) {
                if (appendf(buf, size, &len, "COALESCE(?, CURRENT_TIMESTAMP)") != 0) return -1;
            } else {
                if (appendf(buf, size, &len, "COALESCE(?, %s)", col->default_val) != 0) return -1;
            }
        } else {
            if (appendf(buf, size, &len, "?") != 0) return -1;
        }

        if (i < table->column_count - 1 && appendf(buf, size, &len, ", ") != 0) return -1;
    }

    if (appendf(buf, size, &len, ");") != 0) return -1;
    return 0;
}

static int build_get_by_id_sql(const table_t *table, char *buf, size_t size) {
    size_t len = 0;
    int i;
    int pk = find_primary_key_column(table);

    if (pk < 0) return -1;

    if (appendf(buf, size, &len, "SELECT ") != 0) return -1;
    for (i = 0; i < table->column_count; i++) {
        if (appendf(buf, size, &len, "%s%s",
                    table->columns[i].name,
                    i < table->column_count - 1 ? ", " : "") != 0) {
            return -1;
        }
    }
    if (appendf(buf, size, &len, " FROM %s WHERE %s = ? LIMIT 1;",
                table->name,
                table->columns[pk].name) != 0) {
        return -1;
    }

    return 0;
}

static int build_delete_sql(const table_t *table, char *buf, size_t size) {
    size_t len = 0;
    int pk = find_primary_key_column(table);

    if (pk < 0) return -1;
    if (appendf(buf, size, &len, "DELETE FROM %s WHERE %s = ?;",
                table->name,
                table->columns[pk].name) != 0) {
        return -1;
    }
    return 0;
}

/* ── Parser ──────────────────────────────────────────────────────── */

static int parse_sql(const char *filename) {
    FILE *f = fopen(filename, "r");
    char line[MAX_LINE];
    int in_table = 0;
    int in_query = 0;
    table_t *current_table = NULL;
    query_t *current_query = NULL;

    if (!f) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", filename, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        if (strncmp(line, "table ", 6) == 0) {
            char *name = line + 6;
            char *brace = strchr(name, '{');

            if (table_count >= MAX_TABLES) {
                fprintf(stderr, "Error: Too many tables in %s\n", filename);
                fclose(f);
                return -1;
            }

            if (brace) *brace = '\0';
            trim(name);

            current_table = &tables[table_count++];
            memset(current_table, 0, sizeof(*current_table));
            strncpy(current_table->name, name, MAX_NAME - 1);
            in_table = 1;
            continue;
        }

        if (strncmp(line, "unique index ", 13) == 0 || strncmp(line, "index ", 6) == 0) {
            char *p;
            char *on;
            char *lparen;
            char *rparen;
            index_t *idx;

            if (index_count >= MAX_INDEXES) {
                fprintf(stderr, "Error: Too many indexes in %s\n", filename);
                fclose(f);
                return -1;
            }

            idx = &indexes[index_count++];
            memset(idx, 0, sizeof(*idx));

            if (strncmp(line, "unique index ", 13) == 0) {
                idx->is_unique = 1;
                p = line + 13;
            } else {
                p = line + 6;
            }

            on = strstr(p, " on ");
            if (!on) continue;

            *on = '\0';
            strncpy(idx->name, p, MAX_NAME - 1);
            trim(idx->name);

            p = on + 4;
            lparen = strchr(p, '(');
            if (!lparen) continue;

            *lparen = '\0';
            strncpy(idx->table, p, MAX_NAME - 1);
            trim(idx->table);

            rparen = strchr(lparen + 1, ')');
            if (rparen) *rparen = '\0';
            strncpy(idx->columns, lparen + 1, MAX_NAME - 1);
            trim(idx->columns);
            continue;
        }

        if (strncmp(line, "query ", 6) == 0) {
            char *p;
            char *lparen;
            char *rparen;
            char *arrow;

            if (query_count >= MAX_QUERIES) {
                fprintf(stderr, "Error: Too many queries in %s\n", filename);
                fclose(f);
                return -1;
            }

            current_query = &queries[query_count++];
            memset(current_query, 0, sizeof(*current_query));
            p = line + 6;

            lparen = strchr(p, '(');
            if (!lparen) continue;

            *lparen = '\0';
            strncpy(current_query->name, p, MAX_NAME - 1);
            trim(current_query->name);

            rparen = strchr(lparen + 1, ')');
            if (!rparen) continue;

            *rparen = '\0';
            if (lparen[1] != '\0') {
                char *params = lparen + 1;
                char *tok = strtok(params, ",");
                while (tok && current_query->param_count < MAX_PARAMS) {
                    char *colon;
                    param_t *param;

                    trim(tok);
                    colon = strchr(tok, ':');
                    if (!colon) {
                        tok = strtok(NULL, ",");
                        continue;
                    }

                    *colon = '\0';
                    param = &current_query->params[current_query->param_count++];
                    strncpy(param->name, tok, MAX_NAME - 1);
                    trim(param->name);
                    strncpy(param->type, colon + 1, MAX_NAME - 1);
                    trim(param->type);

                    tok = strtok(NULL, ",");
                }
            }

            arrow = strstr(rparen + 1, "->");
            if (arrow) {
                char *ret = arrow + 2;
                char *brace = strchr(ret, '{');
                if (brace) *brace = '\0';
                trim(ret);
                strncpy(current_query->return_type, ret, MAX_NAME - 1);
            }

            in_query = 1;
            continue;
        }

        if (line[0] == '}') {
            in_table = 0;
            in_query = 0;
            current_table = NULL;
            current_query = NULL;
            continue;
        }

        if (in_table && current_table) {
            char *colon = strchr(line, ':');
            column_t *col;

            if (!colon || current_table->column_count >= MAX_COLUMNS) continue;

            col = &current_table->columns[current_table->column_count++];
            memset(col, 0, sizeof(*col));

            *colon = '\0';
            strncpy(col->name, line, MAX_NAME - 1);
            trim(col->name);

            {
                char *rest = colon + 1;
                char *tok;
                trim(rest);
                tok = strtok(rest, " ");
                if (tok) {
                    strncpy(col->type, tok, MAX_NAME - 1);
                    while ((tok = strtok(NULL, " ")) != NULL) {
                        if (strcmp(tok, "primary") == 0) {
                            col->is_primary = 1;
                        } else if (strcmp(tok, "key") == 0) {
                            continue;
                        } else if (strcmp(tok, "unique") == 0) {
                            col->is_unique = 1;
                        } else if (strcmp(tok, "not") == 0) {
                            col->is_not_null = 1;
                        } else if (strcmp(tok, "null") == 0) {
                            continue;
                        } else if (strcmp(tok, "default") == 0) {
                            tok = strtok(NULL, " ");
                            if (tok) strncpy(col->default_val, tok, MAX_NAME - 1);
                        } else if (strcmp(tok, "references") == 0) {
                            tok = strtok(NULL, " ");
                            if (tok) strncpy(col->references, tok, MAX_NAME - 1);
                        }
                    }
                }
            }
            continue;
        }

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

static void emit_query_signature(FILE *out, const char *prefix, const query_t *query) {
    int i;
    table_t *ret_table = query->return_type[0] ? find_table_by_name(query->return_type) : NULL;

    fprintf(out, "int %s_%s(sqlite3 *db", prefix, query->name);
    for (i = 0; i < query->param_count; i++) {
        const param_t *param = &query->params[i];
        fprintf(out, ", %s %s", sql_type_to_c_param(param->type), param->name);
        if (is_sql_blob_type(param->type)) {
            fprintf(out, ", int %s_bytes", param->name);
        }
    }
    if (ret_table) {
        fprintf(out, ", %s_%s_row_t *out", prefix, ret_table->name);
    }
    fprintf(out, ")");
}

static void emit_row_decode(FILE *out, const char *row_name, const char *prefix, const table_t *table) {
    int i;

    for (i = 0; i < table->column_count; i++) {
        const column_t *col = &table->columns[i];

        if (is_sql_text_type(col->type) || is_sql_timestamp_type(col->type)) {
            fprintf(out, "            {\n");
            fprintf(out, "                const unsigned char *value = sqlite3_column_text(stmt, %d);\n", i);
            fprintf(out, "                if (value) {\n");
            fprintf(out, "                    %s.%s = %s_strdup_text(value);\n", row_name, col->name, prefix);
            fprintf(out, "                    if (!%s.%s) {\n", row_name, col->name);
            fprintf(out, "                        rc = SQLITE_NOMEM;\n");
            fprintf(out, "                    } else {\n");
            fprintf(out, "                        %s._owned_mask |= (1u << %d);\n", row_name, i);
            fprintf(out, "                    }\n");
            fprintf(out, "                }\n");
            fprintf(out, "            }\n");
        } else if (is_sql_blob_type(col->type)) {
            fprintf(out, "            {\n");
            fprintf(out, "                int bytes = sqlite3_column_bytes(stmt, %d);\n", i);
            fprintf(out, "                const void *value = sqlite3_column_blob(stmt, %d);\n", i);
            fprintf(out, "                if (value && bytes > 0) {\n");
            fprintf(out, "                    %s.%s = %s_dup_blob(value, bytes);\n", row_name, col->name, prefix);
            fprintf(out, "                    if (!%s.%s) {\n", row_name, col->name);
            fprintf(out, "                        rc = SQLITE_NOMEM;\n");
            fprintf(out, "                    } else {\n");
            fprintf(out, "                        %s.%s_bytes = bytes;\n", row_name, col->name);
            fprintf(out, "                        %s._owned_mask |= (1u << %d);\n", row_name, i);
            fprintf(out, "                    }\n");
            fprintf(out, "                }\n");
            fprintf(out, "            }\n");
        } else if (is_sql_real_type(col->type)) {
            fprintf(out, "            %s.%s = sqlite3_column_double(stmt, %d);\n", row_name, col->name, i);
        } else if (strcmp(col->type, "boolean") == 0) {
            fprintf(out, "            %s.%s = sqlite3_column_int(stmt, %d);\n", row_name, col->name, i);
        } else {
            fprintf(out, "            %s.%s = (int64_t)sqlite3_column_int64(stmt, %d);\n", row_name, col->name, i);
        }
    }
}

static int generate_schema_sql(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char stmt[MAX_SQL];
    FILE *out;
    int i;

    snprintf(path, sizeof(path), "%s/%s_schema.sql", outdir, prefix);
    out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    fprintf(out, "-- AUTO-GENERATED by sqlgen %s — DO NOT EDIT\n", SQLGEN_VERSION);
    fprintf(out, "-- Regenerate: make regen\n\n");

    for (i = 0; i < table_count; i++) {
        if (build_create_table_sql(&tables[i], stmt, sizeof(stmt)) != 0) {
            fclose(out);
            return -1;
        }
        fputs(stmt, out);
        fputc('\n', out);
    }

    for (i = 0; i < index_count; i++) {
        if (build_index_sql(&indexes[i], stmt, sizeof(stmt)) != 0) {
            fclose(out);
            return -1;
        }
        fputs(stmt, out);
    }

    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_db_h(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char upper[MAX_NAME];
    FILE *out;
    time_t now;
    int i;

    snprintf(path, sizeof(path), "%s/%s_db.h", outdir, prefix);
    out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    strncpy(upper, prefix, MAX_NAME - 1);
    upper[MAX_NAME - 1] = '\0';
    to_upper(upper);

    now = time(NULL);
    fprintf(out, "/* AUTO-GENERATED by sqlgen %s — DO NOT EDIT\n", SQLGEN_VERSION);
    fprintf(out, " * @generated %s", ctime(&now));
    fprintf(out, " * Regenerate: make regen\n");
    fprintf(out, " */\n\n");
    fprintf(out, "#ifndef %s_DB_H\n", upper);
    fprintf(out, "#define %s_DB_H\n\n", upper);
    fprintf(out, "#include <sqlite3.h>\n");
    fprintf(out, "#include <stdint.h>\n\n");

    for (i = 0; i < table_count; i++) {
        int j;
        const table_t *table = &tables[i];

        fprintf(out, "/* Row struct for %s */\n", table->name);
        fprintf(out, "typedef struct {\n");
        for (j = 0; j < table->column_count; j++) {
            const column_t *col = &table->columns[j];
            fprintf(out, "    %s %s;\n", sql_type_to_c_field(col->type), col->name);
            if (is_sql_blob_type(col->type)) {
                fprintf(out, "    int %s_bytes;\n", col->name);
            }
        }
        fprintf(out, "    uint32_t _owned_mask;\n");
        fprintf(out, "} %s_%s_row_t;\n", prefix, table->name);
        fprintf(out, "void %s_%s_row_init(%s_%s_row_t *row);\n",
                prefix, table->name, prefix, table->name);
        fprintf(out, "void %s_%s_row_dispose(%s_%s_row_t *row);\n\n",
                prefix, table->name, prefix, table->name);
    }

    fprintf(out, "/* Database lifecycle */\n");
    fprintf(out, "int %s_db_init(sqlite3 **db, const char *path);\n", prefix);
    fprintf(out, "void %s_db_close(sqlite3 *db);\n\n", prefix);

    for (i = 0; i < table_count; i++) {
        const table_t *table = &tables[i];
        fprintf(out, "/* CRUD for %s */\n", table->name);
        fprintf(out, "int %s_%s_insert(sqlite3 *db, const %s_%s_row_t *row);\n",
                prefix, table->name, prefix, table->name);
        fprintf(out, "int %s_%s_get_by_id(sqlite3 *db, int64_t id, %s_%s_row_t *out);\n",
                prefix, table->name, prefix, table->name);
        fprintf(out, "int %s_%s_delete(sqlite3 *db, int64_t id);\n\n",
                prefix, table->name);
    }

    if (query_count > 0) {
        fprintf(out, "/* Custom queries */\n");
        for (i = 0; i < query_count; i++) {
            emit_query_signature(out, prefix, &queries[i]);
            fprintf(out, ";\n");
        }
    }

    fprintf(out, "\n#endif /* %s_DB_H */\n", upper);
    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_db_c(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char stmt_sql[MAX_SQL];
    FILE *out;
    time_t now;
    int i;

    snprintf(path, sizeof(path), "%s/%s_db.c", outdir, prefix);
    out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    now = time(NULL);
    fprintf(out, "/* AUTO-GENERATED by sqlgen %s — DO NOT EDIT\n", SQLGEN_VERSION);
    fprintf(out, " * @generated %s", ctime(&now));
    fprintf(out, " * Regenerate: make regen\n");
    fprintf(out, " */\n\n");
    fprintf(out, "#include \"%s_db.h\"\n", prefix);
    fprintf(out, "#include <string.h>\n\n");

    fprintf(out, "static char *%s_strdup_text(const unsigned char *value) {\n", prefix);
    fprintf(out, "    if (!value) return NULL;\n");
    fprintf(out, "    return sqlite3_mprintf(\"%%s\", (const char *)value);\n");
    fprintf(out, "}\n\n");

    fprintf(out, "static void *%s_dup_blob(const void *value, int bytes) {\n", prefix);
    fprintf(out, "    void *copy;\n");
    fprintf(out, "    if (!value || bytes <= 0) return NULL;\n");
    fprintf(out, "    copy = sqlite3_malloc(bytes);\n");
    fprintf(out, "    if (!copy) return NULL;\n");
    fprintf(out, "    memcpy(copy, value, (size_t)bytes);\n");
    fprintf(out, "    return copy;\n");
    fprintf(out, "}\n\n");

    fprintf(out, "static const char *%s_schema_sql =\n", prefix);
    for (i = 0; i < table_count; i++) {
        if (build_create_table_sql(&tables[i], stmt_sql, sizeof(stmt_sql)) != 0) {
            fclose(out);
            return -1;
        }
        fprintf(out, "    ");
        emit_c_string_literal(out, stmt_sql);
        fprintf(out, "\n");
    }
    for (i = 0; i < index_count; i++) {
        if (build_index_sql(&indexes[i], stmt_sql, sizeof(stmt_sql)) != 0) {
            fclose(out);
            return -1;
        }
        fprintf(out, "    ");
        emit_c_string_literal(out, stmt_sql);
        fprintf(out, "\n");
    }
    fprintf(out, ";\n\n");

    for (i = 0; i < table_count; i++) {
        int j;
        const table_t *table = &tables[i];

        fprintf(out, "void %s_%s_row_init(%s_%s_row_t *row) {\n",
                prefix, table->name, prefix, table->name);
        fprintf(out, "    if (!row) return;\n");
        fprintf(out, "    memset(row, 0, sizeof(*row));\n");
        fprintf(out, "}\n\n");

        fprintf(out, "void %s_%s_row_dispose(%s_%s_row_t *row) {\n",
                prefix, table->name, prefix, table->name);
        fprintf(out, "    if (!row) return;\n");
        for (j = 0; j < table->column_count; j++) {
            const column_t *col = &table->columns[j];
            if (is_sql_text_type(col->type) || is_sql_timestamp_type(col->type) || is_sql_blob_type(col->type)) {
                fprintf(out, "    if (row->_owned_mask & (1u << %d)) sqlite3_free(row->%s);\n", j, col->name);
            }
        }
        fprintf(out, "    memset(row, 0, sizeof(*row));\n");
        fprintf(out, "}\n\n");
    }

    fprintf(out, "int %s_db_init(sqlite3 **db, const char *path) {\n", prefix);
    fprintf(out, "    int rc;\n");
    fprintf(out, "    if (!db || !path) return SQLITE_MISUSE;\n");
    fprintf(out, "    rc = sqlite3_open(path, db);\n");
    fprintf(out, "    if (rc != SQLITE_OK) return rc;\n");
    fprintf(out, "    rc = sqlite3_exec(*db, %s_schema_sql, NULL, NULL, NULL);\n", prefix);
    fprintf(out, "    if (rc != SQLITE_OK) {\n");
    fprintf(out, "        sqlite3_close(*db);\n");
    fprintf(out, "        *db = NULL;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return rc;\n");
    fprintf(out, "}\n\n");

    fprintf(out, "void %s_db_close(sqlite3 *db) {\n", prefix);
    fprintf(out, "    if (db) sqlite3_close(db);\n");
    fprintf(out, "}\n\n");

    for (i = 0; i < table_count; i++) {
        int j;
        char insert_sql[MAX_SQL];
        char select_sql[MAX_SQL];
        char delete_sql[MAX_SQL];
        const table_t *table = &tables[i];

        if (build_insert_sql(table, insert_sql, sizeof(insert_sql)) != 0 ||
            build_get_by_id_sql(table, select_sql, sizeof(select_sql)) != 0 ||
            build_delete_sql(table, delete_sql, sizeof(delete_sql)) != 0) {
            fclose(out);
            return -1;
        }

        fprintf(out, "int %s_%s_insert(sqlite3 *db, const %s_%s_row_t *row) {\n",
                prefix, table->name, prefix, table->name);
        fprintf(out, "    static const char *sql = ");
        emit_c_string_literal(out, insert_sql);
        fprintf(out, ";\n");
        fprintf(out, "    sqlite3_stmt *stmt = NULL;\n");
        fprintf(out, "    int rc;\n");
        fprintf(out, "    if (!db || !row) return SQLITE_MISUSE;\n");
        fprintf(out, "    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);\n");
        fprintf(out, "    if (rc != SQLITE_OK) return rc;\n");
        for (j = 0; j < table->column_count; j++) {
            const column_t *col = &table->columns[j];
            int bind_index = j + 1;
            if (is_sql_text_type(col->type) || is_sql_timestamp_type(col->type)) {
                fprintf(out, "    if (rc == SQLITE_OK) rc = row->%s ? sqlite3_bind_text(stmt, %d, row->%s, -1, SQLITE_STATIC) : sqlite3_bind_null(stmt, %d);\n",
                        col->name, bind_index, col->name, bind_index);
            } else if (is_sql_blob_type(col->type)) {
                fprintf(out, "    if (rc == SQLITE_OK) rc = row->%s ? sqlite3_bind_blob(stmt, %d, row->%s, row->%s_bytes, SQLITE_STATIC) : sqlite3_bind_null(stmt, %d);\n",
                        col->name, bind_index, col->name, col->name, bind_index);
            } else if (is_sql_real_type(col->type)) {
                if (col->default_val[0]) {
                    fprintf(out, "    if (rc == SQLITE_OK) rc = row->%s != 0.0 ? sqlite3_bind_double(stmt, %d, row->%s) : sqlite3_bind_null(stmt, %d);\n",
                            col->name, bind_index, col->name, bind_index);
                } else {
                    fprintf(out, "    if (rc == SQLITE_OK) rc = sqlite3_bind_double(stmt, %d, row->%s);\n",
                            bind_index, col->name);
                }
            } else if (strcmp(col->type, "boolean") == 0) {
                fprintf(out, "    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, %d, row->%s);\n",
                        bind_index, col->name);
            } else {
                if ((col->is_primary && strcmp(col->type, "integer") == 0) || col->default_val[0]) {
                    fprintf(out, "    if (rc == SQLITE_OK) rc = row->%s != 0 ? sqlite3_bind_int64(stmt, %d, row->%s) : sqlite3_bind_null(stmt, %d);\n",
                            col->name, bind_index, col->name, bind_index);
                } else {
                    fprintf(out, "    if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, %d, row->%s);\n",
                            bind_index, col->name);
                }
            }
        }
        fprintf(out, "    if (rc == SQLITE_OK) {\n");
        fprintf(out, "        rc = sqlite3_step(stmt);\n");
        fprintf(out, "        if (rc == SQLITE_DONE) rc = SQLITE_OK;\n");
        fprintf(out, "    }\n");
        fprintf(out, "    if (stmt) {\n");
        fprintf(out, "        int finalize_rc = sqlite3_finalize(stmt);\n");
        fprintf(out, "        if (rc == SQLITE_OK && finalize_rc != SQLITE_OK) rc = finalize_rc;\n");
        fprintf(out, "    }\n");
        fprintf(out, "    return rc;\n");
        fprintf(out, "}\n\n");

        fprintf(out, "int %s_%s_get_by_id(sqlite3 *db, int64_t id, %s_%s_row_t *out) {\n",
                prefix, table->name, prefix, table->name);
        fprintf(out, "    static const char *sql = ");
        emit_c_string_literal(out, select_sql);
        fprintf(out, ";\n");
        fprintf(out, "    sqlite3_stmt *stmt = NULL;\n");
        fprintf(out, "    %s_%s_row_t row;\n", prefix, table->name);
        fprintf(out, "    int rc;\n");
        fprintf(out, "    int transferred = 0;\n");
        fprintf(out, "    if (!db || !out) return SQLITE_MISUSE;\n");
        fprintf(out, "    %s_%s_row_init(&row);\n", prefix, table->name);
        fprintf(out, "    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);\n");
        fprintf(out, "    if (rc != SQLITE_OK) return rc;\n");
        fprintf(out, "    rc = sqlite3_bind_int64(stmt, 1, id);\n");
        fprintf(out, "    if (rc == SQLITE_OK) {\n");
        fprintf(out, "        rc = sqlite3_step(stmt);\n");
        fprintf(out, "        if (rc == SQLITE_ROW) {\n");
        emit_row_decode(out, "row", prefix, table);
        fprintf(out, "            if (rc == SQLITE_OK) {\n");
        fprintf(out, "                *out = row;\n");
        fprintf(out, "                transferred = 1;\n");
        fprintf(out, "                rc = SQLITE_OK;\n");
        fprintf(out, "            }\n");
        fprintf(out, "        } else if (rc == SQLITE_DONE) {\n");
        fprintf(out, "            rc = SQLITE_NOTFOUND;\n");
        fprintf(out, "        }\n");
        fprintf(out, "    }\n");
        fprintf(out, "    if (!transferred) %s_%s_row_dispose(&row);\n", prefix, table->name);
        fprintf(out, "    if (stmt) {\n");
        fprintf(out, "        int finalize_rc = sqlite3_finalize(stmt);\n");
        fprintf(out, "        if (rc == SQLITE_OK && finalize_rc != SQLITE_OK) rc = finalize_rc;\n");
        fprintf(out, "    }\n");
        fprintf(out, "    return rc;\n");
        fprintf(out, "}\n\n");

        fprintf(out, "int %s_%s_delete(sqlite3 *db, int64_t id) {\n", prefix, table->name);
        fprintf(out, "    static const char *sql = ");
        emit_c_string_literal(out, delete_sql);
        fprintf(out, ";\n");
        fprintf(out, "    sqlite3_stmt *stmt = NULL;\n");
        fprintf(out, "    int rc;\n");
        fprintf(out, "    if (!db) return SQLITE_MISUSE;\n");
        fprintf(out, "    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);\n");
        fprintf(out, "    if (rc != SQLITE_OK) return rc;\n");
        fprintf(out, "    rc = sqlite3_bind_int64(stmt, 1, id);\n");
        fprintf(out, "    if (rc == SQLITE_OK) {\n");
        fprintf(out, "        rc = sqlite3_step(stmt);\n");
        fprintf(out, "        if (rc == SQLITE_DONE) {\n");
        fprintf(out, "            rc = sqlite3_changes(db) > 0 ? SQLITE_OK : SQLITE_NOTFOUND;\n");
        fprintf(out, "        }\n");
        fprintf(out, "    }\n");
        fprintf(out, "    if (stmt) {\n");
        fprintf(out, "        int finalize_rc = sqlite3_finalize(stmt);\n");
        fprintf(out, "        if (rc == SQLITE_OK && finalize_rc != SQLITE_OK) rc = finalize_rc;\n");
        fprintf(out, "    }\n");
        fprintf(out, "    return rc;\n");
        fprintf(out, "}\n\n");
    }

    for (i = 0; i < query_count; i++) {
        int j;
        const query_t *query = &queries[i];
        table_t *ret_table = query->return_type[0] ? find_table_by_name(query->return_type) : NULL;

        emit_query_signature(out, prefix, query);
        fprintf(out, " {\n");
        fprintf(out, "    static const char *sql = ");
        emit_c_string_literal(out, query->sql);
        fprintf(out, ";\n");
        fprintf(out, "    sqlite3_stmt *stmt = NULL;\n");
        fprintf(out, "    int rc;\n");
        if (ret_table) {
            fprintf(out, "    %s_%s_row_t row;\n", prefix, ret_table->name);
            fprintf(out, "    int transferred = 0;\n");
        }
        fprintf(out, "    if (!db");
        if (ret_table) fprintf(out, " || !out");
        fprintf(out, ") return SQLITE_MISUSE;\n");
        if (ret_table) {
            fprintf(out, "    %s_%s_row_init(&row);\n", prefix, ret_table->name);
        }
        fprintf(out, "    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);\n");
        fprintf(out, "    if (rc != SQLITE_OK) return rc;\n");
        for (j = 0; j < query->param_count; j++) {
            const param_t *param = &query->params[j];
            int bind_index = j + 1;
            if (is_sql_text_type(param->type) || is_sql_timestamp_type(param->type)) {
                fprintf(out, "    if (rc == SQLITE_OK) rc = %s ? sqlite3_bind_text(stmt, %d, %s, -1, SQLITE_STATIC) : sqlite3_bind_null(stmt, %d);\n",
                        param->name, bind_index, param->name, bind_index);
            } else if (is_sql_blob_type(param->type)) {
                fprintf(out, "    if (rc == SQLITE_OK) rc = %s ? sqlite3_bind_blob(stmt, %d, %s, %s_bytes, SQLITE_STATIC) : sqlite3_bind_null(stmt, %d);\n",
                        param->name, bind_index, param->name, param->name, bind_index);
            } else if (is_sql_real_type(param->type)) {
                fprintf(out, "    if (rc == SQLITE_OK) rc = sqlite3_bind_double(stmt, %d, %s);\n",
                        bind_index, param->name);
            } else if (strcmp(param->type, "boolean") == 0) {
                fprintf(out, "    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, %d, %s);\n",
                        bind_index, param->name);
            } else {
                fprintf(out, "    if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, %d, %s);\n",
                        bind_index, param->name);
            }
        }
        fprintf(out, "    if (rc == SQLITE_OK) {\n");
        fprintf(out, "        rc = sqlite3_step(stmt);\n");
        if (ret_table) {
            fprintf(out, "        if (rc == SQLITE_ROW) {\n");
            emit_row_decode(out, "row", prefix, ret_table);
            fprintf(out, "            if (rc == SQLITE_OK) {\n");
            fprintf(out, "                *out = row;\n");
            fprintf(out, "                transferred = 1;\n");
            fprintf(out, "                rc = SQLITE_OK;\n");
            fprintf(out, "            }\n");
            fprintf(out, "        } else if (rc == SQLITE_DONE) {\n");
            fprintf(out, "            rc = SQLITE_NOTFOUND;\n");
            fprintf(out, "        }\n");
        } else {
            fprintf(out, "        if (rc == SQLITE_DONE) rc = SQLITE_OK;\n");
        }
        fprintf(out, "    }\n");
        if (ret_table) {
            fprintf(out, "    if (!transferred) %s_%s_row_dispose(&row);\n", prefix, ret_table->name);
        }
        fprintf(out, "    if (stmt) {\n");
        fprintf(out, "        int finalize_rc = sqlite3_finalize(stmt);\n");
        fprintf(out, "        if (rc == SQLITE_OK && finalize_rc != SQLITE_OK) rc = finalize_rc;\n");
        fprintf(out, "    }\n");
        fprintf(out, "    return rc;\n");
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
    char prefix[MAX_NAME];
    const char *input;
    const char *outdir;
    const char *basename;
    char *dot;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    input = argv[1];
    outdir = argc > 2 ? argv[2] : ".";

    basename = strrchr(input, '/');
    basename = basename ? basename + 1 : input;
    strncpy(prefix, basename, MAX_NAME - 1);
    prefix[MAX_NAME - 1] = '\0';
    dot = strchr(prefix, '.');
    if (dot) *dot = '\0';

    if (argc > 3) {
        strncpy(prefix, argv[3], MAX_NAME - 1);
        prefix[MAX_NAME - 1] = '\0';
    }

    if (parse_sql(input) != 0) return 1;

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
