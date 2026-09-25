/* ═══════════════════════════════════════════════════════════════════════════
 * schemagen — Schema-Driven Code Generator
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * cosmo-bde — BDE with Models
 * Ring 0: Pure C, compiles with cosmocc to APE
 *
 * Generates multiple output formats from .schema specs:
 *   --c      C types, init, validate (default)
 *   --json   C + JSON serialization (yyjson)
 *   --sql    C + SQLite bindings
 *   --proto  Protocol Buffers .proto file
 *   --fbs    FlatBuffers .fbs file
 *   --all    All formats
 *
 * Usage: schemagen [options] <input.schema> <output_dir> [prefix]
 *
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include "cosmo_harden.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>

#define SCHEMAGEN_VERSION "2.0.0"
#define MAX_LINE 1024
#define MAX_TYPES 256
#define MAX_FIELDS 64
#define MAX_NAME 64

/* ── Output Modes ──────────────────────────────────────────────────────────── */

typedef enum {
    OUT_C      = 1 << 0,
    OUT_JSON   = 1 << 1,
    OUT_SQL    = 1 << 2,
    OUT_PROTO  = 1 << 3,
    OUT_FBS    = 1 << 4,
    OUT_ALL    = 0xFF
} output_mode_t;

/* ── Type System ───────────────────────────────────────────────────────────── */

typedef enum {
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_F32, TYPE_F64,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_STRUCT,
    TYPE_ARRAY,
    TYPE_POINTER,
} base_type_t;

typedef struct {
    char name[MAX_NAME];
    base_type_t base;
    char struct_name[MAX_NAME];
    int array_size;
    int is_pointer;
    int has_range;
    int64_t range_min, range_max;
    int has_default;
    int64_t default_val;
    int not_empty;
    char doc[256];
} field_t;

typedef struct {
    char name[MAX_NAME];
    field_t fields[MAX_FIELDS];
    int field_count;
    char doc[256];
} type_def_t;

static type_def_t types[MAX_TYPES];
static int type_count = 0;

/* ── Utilities ─────────────────────────────────────────────────────────────── */

static void safe_strcpy(char *dest, const char *src, size_t dest_size) {
    if (dest_size == 0) return;
    size_t i;
    for (i = 0; i < dest_size - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static void trim(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
}

static void to_lower(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* Sanitize string for use as C identifier (replace non-alnum with _) */
static void sanitize_c_ident(char *s) {
    for (; *s; s++) {
        if (!isalnum((unsigned char)*s)) *s = '_';
    }
}

static void to_snake_case(char *dest, const char *src, size_t size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < size - 1; i++) {
        if (isupper((unsigned char)src[i]) && i > 0) {
            if (j < size - 2) dest[j++] = '_';
        }
        dest[j++] = (char)tolower((unsigned char)src[i]);
    }
    dest[j] = '\0';
}

/* ── Type Mapping ──────────────────────────────────────────────────────────── */

static const char* base_type_to_c(base_type_t t) {
    switch (t) {
        case TYPE_I8:  return "int8_t";
        case TYPE_I16: return "int16_t";
        case TYPE_I32: return "int32_t";
        case TYPE_I64: return "int64_t";
        case TYPE_U8:  return "uint8_t";
        case TYPE_U16: return "uint16_t";
        case TYPE_U32: return "uint32_t";
        case TYPE_U64: return "uint64_t";
        case TYPE_F32: return "float";
        case TYPE_F64: return "double";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "char";
        default: return "void";
    }
}

static const char* base_type_to_proto(base_type_t t) {
    switch (t) {
        case TYPE_I8:  case TYPE_I16: case TYPE_I32: return "int32";
        case TYPE_I64: return "int64";
        case TYPE_U8:  case TYPE_U16: case TYPE_U32: return "uint32";
        case TYPE_U64: return "uint64";
        case TYPE_F32: return "float";
        case TYPE_F64: return "double";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        default: return "bytes";
    }
}

static const char* base_type_to_fbs(base_type_t t) {
    switch (t) {
        case TYPE_I8:  return "int8";
        case TYPE_I16: return "int16";
        case TYPE_I32: return "int32";
        case TYPE_I64: return "int64";
        case TYPE_U8:  return "uint8";
        case TYPE_U16: return "uint16";
        case TYPE_U32: return "uint32";
        case TYPE_U64: return "uint64";
        case TYPE_F32: return "float";
        case TYPE_F64: return "double";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        default: return "ubyte";
    }
}

static const char* base_type_to_sql(base_type_t t) {
    switch (t) {
        case TYPE_I8:  case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_U8:  case TYPE_U16: case TYPE_U32: case TYPE_U64:
            return "INTEGER";
        case TYPE_F32: case TYPE_F64: return "REAL";
        case TYPE_BOOL: return "INTEGER";
        case TYPE_STRING: return "TEXT";
        default: return "BLOB";
    }
}

static base_type_t parse_base_type(const char *s) {
    if (strcmp(s, "i8") == 0)  return TYPE_I8;
    if (strcmp(s, "i16") == 0) return TYPE_I16;
    if (strcmp(s, "i32") == 0) return TYPE_I32;
    if (strcmp(s, "i64") == 0) return TYPE_I64;
    if (strcmp(s, "u8") == 0)  return TYPE_U8;
    if (strcmp(s, "u16") == 0) return TYPE_U16;
    if (strcmp(s, "u32") == 0) return TYPE_U32;
    if (strcmp(s, "u64") == 0) return TYPE_U64;
    if (strcmp(s, "f32") == 0) return TYPE_F32;
    if (strcmp(s, "f64") == 0) return TYPE_F64;
    if (strcmp(s, "bool") == 0) return TYPE_BOOL;
    if (strncmp(s, "string", 6) == 0) return TYPE_STRING;
    return TYPE_STRUCT;
}

/* ── Parser ────────────────────────────────────────────────────────────────── */

static int parse_field(const char *line, field_t *f) {
    memset(f, 0, sizeof(*f));

    char name[MAX_NAME], type_str[MAX_NAME];
    const char *colon = strchr(line, ':');
    if (!colon) return -1;

    int name_len = (int)(colon - line);
    if (name_len >= MAX_NAME) name_len = MAX_NAME - 1;
    memcpy(name, line, (size_t)name_len);
    name[name_len] = '\0';
    trim(name);
    safe_strcpy(f->name, name, MAX_NAME);

    const char *type_start = colon + 1;
    while (*type_start && isspace((unsigned char)*type_start)) type_start++;

    const char *bracket = strchr(type_start, '[');
    const char *space = strchr(type_start, ' ');
    const char *type_end = bracket ? bracket : (space ? space : type_start + strlen(type_start));

    int type_len = (int)(type_end - type_start);
    if (type_len >= MAX_NAME) type_len = MAX_NAME - 1;
    memcpy(type_str, type_start, (size_t)type_len);
    type_str[type_len] = '\0';
    trim(type_str);

    size_t len = strlen(type_str);
    if (len > 0 && type_str[len-1] == '*') {
        f->is_pointer = 1;
        type_str[len-1] = '\0';
        trim(type_str);
    }

    f->base = parse_base_type(type_str);
    if (f->base == TYPE_STRUCT) {
        safe_strcpy(f->struct_name, type_str, MAX_NAME);
    }

    if (bracket) {
        int size = 0;
        sscanf(bracket, "[%d]", &size);
        f->array_size = size;
    }

    const char *constraint = strstr(line, "range:");
    if (constraint) {
        sscanf(constraint, "range: %" SCNd64 "..%" SCNd64, &f->range_min, &f->range_max);
        f->has_range = 1;
    }

    constraint = strstr(line, "default:");
    if (constraint) {
        sscanf(constraint, "default: %" SCNd64, &f->default_val);
        f->has_default = 1;
    }

    if (strstr(line, "not_empty")) {
        f->not_empty = 1;
    }

    /* Extract doc string */
    const char *doc = strstr(line, "doc:");
    if (doc) {
        doc += 4;
        while (*doc && isspace((unsigned char)*doc)) doc++;
        if (*doc == '"') doc++;
        const char *end = strchr(doc, '"');
        if (end) {
            size_t doc_len = (size_t)(end - doc);
            if (doc_len >= sizeof(f->doc)) doc_len = sizeof(f->doc) - 1;
            memcpy(f->doc, doc, doc_len);
            f->doc[doc_len] = '\0';
        }
    }

    return 0;
}

static int parse_schema(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", filename);
        return -1;
    }

    char line[MAX_LINE];
    type_def_t *current = NULL;

    while (fgets(line, sizeof(line), f)) {
        trim(line);

        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#') continue;
        if (strncmp(line, "/*", 2) == 0 || strncmp(line, "//", 2) == 0) continue;
        if (line[0] == '*') continue;  /* continuation of block comment */

        if (strncmp(line, "type ", 5) == 0) {
            if (type_count >= MAX_TYPES) {
                fprintf(stderr, "Error: Too many types\n");
                fclose(f);
                return -1;
            }

            current = &types[type_count++];
            memset(current, 0, sizeof(*current));

            char *name_start = line + 5;
            while (*name_start && isspace((unsigned char)*name_start)) name_start++;
            char *brace = strchr(name_start, '{');
            if (brace) *brace = '\0';
            trim(name_start);
            safe_strcpy(current->name, name_start, MAX_NAME);
            continue;
        }

        if (line[0] == '}') {
            current = NULL;
            continue;
        }

        if (current && strchr(line, ':')) {
            if (current->field_count >= MAX_FIELDS) {
                fprintf(stderr, "Error: Too many fields in %s\n", current->name);
                fclose(f);
                return -1;
            }
            parse_field(line, &current->fields[current->field_count++]);
        }
    }

    fclose(f);
    return 0;
}

/* ── C Code Generation ─────────────────────────────────────────────────────── */

static void gen_c_header(FILE *out, const char *guard) {
    fprintf(out, "/* AUTO-GENERATED by schemagen %s — DO NOT EDIT */\n", SCHEMAGEN_VERSION);
    fprintf(out, "#ifndef %s\n", guard);
    fprintf(out, "#define %s\n\n", guard);
    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <stdbool.h>\n");
    fprintf(out, "#include <stddef.h>\n\n");

    for (int i = 0; i < type_count; i++) {
        fprintf(out, "typedef struct %s %s;\n", types[i].name, types[i].name);
    }
    fprintf(out, "\n");

    for (int i = 0; i < type_count; i++) {
        type_def_t *t = &types[i];
        fprintf(out, "struct %s {\n", t->name);
        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            if (f->base == TYPE_STRING) {
                fprintf(out, "    char %s[%d];\n", f->name, f->array_size > 0 ? f->array_size : 256);
            } else if (f->base == TYPE_STRUCT) {
                fprintf(out, "    %s %s%s;\n", f->struct_name, f->is_pointer ? "*" : "", f->name);
            } else {
                fprintf(out, "    %s %s;\n", base_type_to_c(f->base), f->name);
            }
        }
        fprintf(out, "};\n\n");
    }

    for (int i = 0; i < type_count; i++) {
        type_def_t *t = &types[i];
        fprintf(out, "/* %s functions */\n", t->name);
        fprintf(out, "void %s_init(%s *obj);\n", t->name, t->name);
        fprintf(out, "bool %s_validate(const %s *obj);\n\n", t->name, t->name);
    }

    fprintf(out, "#endif /* %s */\n", guard);
}

static void gen_c_impl(FILE *out, const char *header_name) {
    fprintf(out, "/* AUTO-GENERATED by schemagen %s — DO NOT EDIT */\n\n", SCHEMAGEN_VERSION);
    fprintf(out, "#include \"%s\"\n", header_name);
    fprintf(out, "#include <string.h>\n\n");

    for (int i = 0; i < type_count; i++) {
        type_def_t *t = &types[i];

        fprintf(out, "void %s_init(%s *obj) {\n", t->name, t->name);
        fprintf(out, "    memset(obj, 0, sizeof(*obj));\n");
        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            if (f->has_default && f->base != TYPE_STRING) {
                fprintf(out, "    obj->%s = %" PRId64 ";\n", f->name, f->default_val);
            }
        }
        fprintf(out, "}\n\n");

        fprintf(out, "bool %s_validate(const %s *obj) {\n", t->name, t->name);
        fprintf(out, "    if (!obj) return false;\n");
        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            if (f->has_range) {
                fprintf(out, "    if (obj->%s < %" PRId64 " || obj->%s > %" PRId64 ") return false;\n",
                        f->name, f->range_min, f->name, f->range_max);
            }
            if (f->not_empty && f->base == TYPE_STRING) {
                fprintf(out, "    if (obj->%s[0] == '\\0') return false;\n", f->name);
            }
        }
        fprintf(out, "    return true;\n");
        fprintf(out, "}\n\n");
    }
}

/* ── JSON Code Generation ──────────────────────────────────────────────────── */

static void gen_json_header(FILE *out, const char *guard) {
    fprintf(out, "/* AUTO-GENERATED by schemagen %s — DO NOT EDIT */\n", SCHEMAGEN_VERSION);
    fprintf(out, "/* JSON serialization (requires yyjson) */\n");
    fprintf(out, "#ifndef %s_JSON_H\n", guard);
    fprintf(out, "#define %s_JSON_H\n\n", guard);
    fprintf(out, "#include \"%s_types.h\"\n\n", guard);

    for (int i = 0; i < type_count; i++) {
        type_def_t *t = &types[i];
        fprintf(out, "int %s_to_json(const %s *obj, char *buf, size_t size);\n", t->name, t->name);
        fprintf(out, "int %s_from_json(const char *json, %s *obj);\n\n", t->name, t->name);
    }

    fprintf(out, "#endif /* %s_JSON_H */\n", guard);
}

static void gen_json_impl(FILE *out, const char *prefix) {
    fprintf(out, "/* AUTO-GENERATED by schemagen %s — DO NOT EDIT */\n", SCHEMAGEN_VERSION);
    fprintf(out, "/* JSON serialization (requires yyjson) */\n\n");
    fprintf(out, "#include \"%s_json.h\"\n", prefix);
    fprintf(out, "#include <yyjson.h>\n");
    fprintf(out, "#include <string.h>\n\n");

    for (int i = 0; i < type_count; i++) {
        type_def_t *t = &types[i];

        fprintf(out, "int %s_to_json(const %s *obj, char *buf, size_t size) {\n", t->name, t->name);
        fprintf(out, "    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);\n");
        fprintf(out, "    yyjson_mut_val *root = yyjson_mut_obj(doc);\n");
        fprintf(out, "    yyjson_mut_doc_set_root(doc, root);\n\n");

        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            switch (f->base) {
                case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
                    fprintf(out, "    yyjson_mut_obj_add_int(doc, root, \"%s\", obj->%s);\n", f->name, f->name);
                    break;
                case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
                    fprintf(out, "    yyjson_mut_obj_add_uint(doc, root, \"%s\", obj->%s);\n", f->name, f->name);
                    break;
                case TYPE_F32: case TYPE_F64:
                    fprintf(out, "    yyjson_mut_obj_add_real(doc, root, \"%s\", obj->%s);\n", f->name, f->name);
                    break;
                case TYPE_BOOL:
                    fprintf(out, "    yyjson_mut_obj_add_bool(doc, root, \"%s\", obj->%s);\n", f->name, f->name);
                    break;
                case TYPE_STRING:
                    fprintf(out, "    yyjson_mut_obj_add_str(doc, root, \"%s\", obj->%s);\n", f->name, f->name);
                    break;
                default:
                    break;
            }
        }

        fprintf(out, "\n    size_t len = 0;\n");
        fprintf(out, "    char *json_str = yyjson_mut_write(doc, 0, &len);\n");
        fprintf(out, "    yyjson_mut_doc_free(doc);\n");
        fprintf(out, "    if (!json_str) return -1;\n");
        fprintf(out, "    if (len >= size) { free(json_str); return -1; }\n");
        fprintf(out, "    memcpy(buf, json_str, len + 1);\n");
        fprintf(out, "    free(json_str);\n");
        fprintf(out, "    return (int)len;\n");
        fprintf(out, "}\n\n");

        fprintf(out, "int %s_from_json(const char *json, %s *obj) {\n", t->name, t->name);
        fprintf(out, "    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);\n");
        fprintf(out, "    if (!doc) return -1;\n");
        fprintf(out, "    yyjson_val *root = yyjson_doc_get_root(doc);\n\n");

        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            fprintf(out, "    yyjson_val *v_%s = yyjson_obj_get(root, \"%s\");\n", f->name, f->name);
            switch (f->base) {
                case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
                    fprintf(out, "    if (v_%s) obj->%s = yyjson_get_int(v_%s);\n", f->name, f->name, f->name);
                    break;
                case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
                    fprintf(out, "    if (v_%s) obj->%s = yyjson_get_uint(v_%s);\n", f->name, f->name, f->name);
                    break;
                case TYPE_F32: case TYPE_F64:
                    fprintf(out, "    if (v_%s) obj->%s = yyjson_get_real(v_%s);\n", f->name, f->name, f->name);
                    break;
                case TYPE_BOOL:
                    fprintf(out, "    if (v_%s) obj->%s = yyjson_get_bool(v_%s);\n", f->name, f->name, f->name);
                    break;
                case TYPE_STRING:
                    fprintf(out, "    if (v_%s) strncpy(obj->%s, yyjson_get_str(v_%s), sizeof(obj->%s)-1);\n",
                            f->name, f->name, f->name, f->name);
                    break;
                default:
                    break;
            }
        }

        fprintf(out, "\n    yyjson_doc_free(doc);\n");
        fprintf(out, "    return 0;\n");
        fprintf(out, "}\n\n");
    }
}

/* ── SQL Code Generation ───────────────────────────────────────────────────── */

static void gen_sql_header(FILE *out, const char *guard) {
    fprintf(out, "/* AUTO-GENERATED by schemagen %s — DO NOT EDIT */\n", SCHEMAGEN_VERSION);
    fprintf(out, "/* SQLite bindings */\n");
    fprintf(out, "#ifndef %s_SQL_H\n", guard);
    fprintf(out, "#define %s_SQL_H\n\n", guard);
    fprintf(out, "#include \"%s_types.h\"\n", guard);
    fprintf(out, "#include <sqlite3.h>\n\n");

    for (int i = 0; i < type_count; i++) {
        type_def_t *t = &types[i];
        char snake[MAX_NAME];
        to_snake_case(snake, t->name, sizeof(snake));
        fprintf(out, "int %s_create_table(sqlite3 *db);\n", t->name);
        fprintf(out, "int %s_insert(sqlite3 *db, const %s *obj);\n", t->name, t->name);
        fprintf(out, "int %s_select_by_id(sqlite3 *db, int64_t id, %s *obj);\n\n", t->name, t->name);
    }

    fprintf(out, "#endif /* %s_SQL_H */\n", guard);
}

static void gen_sql_impl(FILE *out, const char *prefix) {
    fprintf(out, "/* AUTO-GENERATED by schemagen %s — DO NOT EDIT */\n", SCHEMAGEN_VERSION);
    fprintf(out, "/* SQLite bindings */\n\n");
    fprintf(out, "#include \"%s_sql.h\"\n", prefix);
    fprintf(out, "#include <string.h>\n\n");

    for (int i = 0; i < type_count; i++) {
        type_def_t *t = &types[i];
        char snake[MAX_NAME];
        to_snake_case(snake, t->name, sizeof(snake));

        /* CREATE TABLE */
        fprintf(out, "int %s_create_table(sqlite3 *db) {\n", t->name);
        fprintf(out, "    const char *sql = \"CREATE TABLE IF NOT EXISTS %s (\\n\"\n", snake);
        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            fprintf(out, "        \"    %s %s%s\\n\"\n",
                    f->name, base_type_to_sql(f->base),
                    j < t->field_count - 1 ? "," : "");
        }
        fprintf(out, "        \")\";\n");
        fprintf(out, "    return sqlite3_exec(db, sql, NULL, NULL, NULL);\n");
        fprintf(out, "}\n\n");

        /* INSERT */
        fprintf(out, "int %s_insert(sqlite3 *db, const %s *obj) {\n", t->name, t->name);
        fprintf(out, "    sqlite3_stmt *stmt;\n");
        fprintf(out, "    const char *sql = \"INSERT INTO %s (", snake);
        for (int j = 0; j < t->field_count; j++) {
            fprintf(out, "%s%s", t->fields[j].name, j < t->field_count - 1 ? ", " : "");
        }
        fprintf(out, ") VALUES (");
        for (int j = 0; j < t->field_count; j++) {
            fprintf(out, "?%s", j < t->field_count - 1 ? ", " : "");
        }
        fprintf(out, ")\";\n");
        fprintf(out, "    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;\n");

        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            switch (f->base) {
                case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
                case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
                case TYPE_BOOL:
                    fprintf(out, "    sqlite3_bind_int64(stmt, %d, obj->%s);\n", j+1, f->name);
                    break;
                case TYPE_F32: case TYPE_F64:
                    fprintf(out, "    sqlite3_bind_double(stmt, %d, obj->%s);\n", j+1, f->name);
                    break;
                case TYPE_STRING:
                    fprintf(out, "    sqlite3_bind_text(stmt, %d, obj->%s, -1, SQLITE_STATIC);\n", j+1, f->name);
                    break;
                default:
                    break;
            }
        }

        fprintf(out, "    int rc = sqlite3_step(stmt);\n");
        fprintf(out, "    sqlite3_finalize(stmt);\n");
        fprintf(out, "    return rc == SQLITE_DONE ? 0 : -1;\n");
        fprintf(out, "}\n\n");

        /* SELECT */
        fprintf(out, "int %s_select_by_id(sqlite3 *db, int64_t id, %s *obj) {\n", t->name, t->name);
        fprintf(out, "    sqlite3_stmt *stmt;\n");
        fprintf(out, "    const char *sql = \"SELECT * FROM %s WHERE id = ?\";\n", snake);
        fprintf(out, "    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;\n");
        fprintf(out, "    sqlite3_bind_int64(stmt, 1, id);\n");
        fprintf(out, "    if (sqlite3_step(stmt) != SQLITE_ROW) { sqlite3_finalize(stmt); return -1; }\n");

        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            switch (f->base) {
                case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
                case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
                case TYPE_BOOL:
                    fprintf(out, "    obj->%s = sqlite3_column_int64(stmt, %d);\n", f->name, j);
                    break;
                case TYPE_F32: case TYPE_F64:
                    fprintf(out, "    obj->%s = sqlite3_column_double(stmt, %d);\n", f->name, j);
                    break;
                case TYPE_STRING:
                    fprintf(out, "    strncpy(obj->%s, (const char*)sqlite3_column_text(stmt, %d), sizeof(obj->%s)-1);\n",
                            f->name, j, f->name);
                    break;
                default:
                    break;
            }
        }

        fprintf(out, "    sqlite3_finalize(stmt);\n");
        fprintf(out, "    return 0;\n");
        fprintf(out, "}\n\n");
    }
}

/* ── Protocol Buffers Generation ───────────────────────────────────────────── */

static void gen_proto(FILE *out, const char *package) {
    fprintf(out, "// AUTO-GENERATED by schemagen %s — DO NOT EDIT\n", SCHEMAGEN_VERSION);
    fprintf(out, "// Compile with: protoc --c_out=. %s.proto\n\n", package);
    fprintf(out, "syntax = \"proto3\";\n\n");
    fprintf(out, "package %s;\n\n", package);

    for (int i = 0; i < type_count; i++) {
        type_def_t *t = &types[i];
        fprintf(out, "message %s {\n", t->name);
        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            const char *proto_type = f->base == TYPE_STRUCT ? f->struct_name : base_type_to_proto(f->base);
            if (f->array_size > 0 && f->base != TYPE_STRING) {
                fprintf(out, "    repeated %s %s = %d;", proto_type, f->name, j+1);
            } else {
                fprintf(out, "    %s %s = %d;", proto_type, f->name, j+1);
            }
            if (f->doc[0]) fprintf(out, " // %s", f->doc);
            fprintf(out, "\n");
        }
        fprintf(out, "}\n\n");
    }
}

/* ── FlatBuffers Generation ────────────────────────────────────────────────── */

static void gen_fbs(FILE *out, const char *ns) {
    fprintf(out, "// AUTO-GENERATED by schemagen %s — DO NOT EDIT\n", SCHEMAGEN_VERSION);
    fprintf(out, "// Compile with: flatcc -a %s.fbs\n\n", ns);
    fprintf(out, "namespace %s;\n\n", ns);

    for (int i = 0; i < type_count; i++) {
        type_def_t *t = &types[i];
        fprintf(out, "table %s {\n", t->name);
        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            const char *fbs_type = f->base == TYPE_STRUCT ? f->struct_name : base_type_to_fbs(f->base);
            if (f->array_size > 0 && f->base != TYPE_STRING) {
                fprintf(out, "    %s:[%s];", f->name, fbs_type);
            } else {
                fprintf(out, "    %s:%s;", f->name, fbs_type);
            }
            if (f->doc[0]) fprintf(out, " // %s", f->doc);
            fprintf(out, "\n");
        }
        fprintf(out, "}\n\n");
    }

    if (type_count > 0) {
        fprintf(out, "root_type %s;\n", types[0].name);
    }
}

/* ── Main ──────────────────────────────────────────────────────────────────── */

static void print_usage(void) {
    fprintf(stderr, "schemagen %s — Multi-Format Code Generator\n", SCHEMAGEN_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: schemagen [options] <input.schema> <output_dir> [prefix]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Output Formats:\n");
    fprintf(stderr, "  --c        C types, init, validate (default)\n");
    fprintf(stderr, "  --json     C + JSON serialization (yyjson)\n");
    fprintf(stderr, "  --sql      C + SQLite bindings\n");
    fprintf(stderr, "  --proto    Protocol Buffers .proto\n");
    fprintf(stderr, "  --fbs      FlatBuffers .fbs\n");
    fprintf(stderr, "  --all      All formats\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Example:\n");
    fprintf(stderr, "  schemagen --all sensor.schema gen/domain sensor\n");
    fprintf(stderr, "  -> sensor_types.h, sensor_types.c\n");
    fprintf(stderr, "  -> sensor_json.h, sensor_json.c\n");
    fprintf(stderr, "  -> sensor_sql.h, sensor_sql.c\n");
    fprintf(stderr, "  -> sensor.proto, sensor.fbs\n");
}

int main(int argc, char *argv[]) {
    tool_harden();
    output_mode_t mode = 0;
    const char *input = NULL;
    const char *outdir = ".";
    const char *prefix = "schema";

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--c") == 0) mode |= OUT_C;
        else if (strcmp(argv[i], "--json") == 0) mode |= OUT_JSON;
        else if (strcmp(argv[i], "--sql") == 0) mode |= OUT_SQL;
        else if (strcmp(argv[i], "--proto") == 0) mode |= OUT_PROTO;
        else if (strcmp(argv[i], "--fbs") == 0) mode |= OUT_FBS;
        else if (strcmp(argv[i], "--all") == 0) mode = OUT_ALL;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
        else if (argv[i][0] != '-') {
            if (!input) input = argv[i];
            else if (strcmp(outdir, ".") == 0) outdir = argv[i];
            else prefix = argv[i];
        }
    }

    if (!input) {
        print_usage();
        return 1;
    }

    if (mode == 0) mode = OUT_C;

    if (parse_schema(input) != 0) return 1;
    fprintf(stderr, "Parsed %d types from %s\n", type_count, input);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", outdir);
    if (system(cmd) != 0) {
        fprintf(stderr, "Warning: mkdir -p %s may have failed\n", outdir);
    }

    char path[512];
    char prefix_lower[MAX_NAME];
    char prefix_safe[MAX_NAME];
    safe_strcpy(prefix_lower, prefix, sizeof(prefix_lower));
    to_lower(prefix_lower);
    sanitize_c_ident(prefix_lower);  /* Make valid C identifier */
    safe_strcpy(prefix_safe, prefix, sizeof(prefix_safe));
    sanitize_c_ident(prefix_safe);   /* Preserve case but sanitize */

    /* C types */
    if (mode & OUT_C) {
        snprintf(path, sizeof(path), "%s/%s_types.h", outdir, prefix_lower);
        FILE *out = fopen(path, "w");
        if (out) { gen_c_header(out, prefix_safe); fclose(out); fprintf(stderr, "Generated %s\n", path); }

        snprintf(path, sizeof(path), "%s/%s_types.c", outdir, prefix_lower);
        out = fopen(path, "w");
        char header[128]; snprintf(header, sizeof(header), "%s_types.h", prefix_lower);
        if (out) { gen_c_impl(out, header); fclose(out); fprintf(stderr, "Generated %s\n", path); }
    }

    /* JSON */
    if (mode & OUT_JSON) {
        snprintf(path, sizeof(path), "%s/%s_json.h", outdir, prefix_lower);
        FILE *out = fopen(path, "w");
        if (out) { gen_json_header(out, prefix_safe); fclose(out); fprintf(stderr, "Generated %s\n", path); }

        snprintf(path, sizeof(path), "%s/%s_json.c", outdir, prefix_lower);
        out = fopen(path, "w");
        if (out) { gen_json_impl(out, prefix_lower); fclose(out); fprintf(stderr, "Generated %s\n", path); }
    }

    /* SQL */
    if (mode & OUT_SQL) {
        snprintf(path, sizeof(path), "%s/%s_sql.h", outdir, prefix_lower);
        FILE *out = fopen(path, "w");
        if (out) { gen_sql_header(out, prefix_safe); fclose(out); fprintf(stderr, "Generated %s\n", path); }

        snprintf(path, sizeof(path), "%s/%s_sql.c", outdir, prefix_lower);
        out = fopen(path, "w");
        if (out) { gen_sql_impl(out, prefix_lower); fclose(out); fprintf(stderr, "Generated %s\n", path); }
    }

    /* Protocol Buffers */
    if (mode & OUT_PROTO) {
        snprintf(path, sizeof(path), "%s/%s.proto", outdir, prefix_lower);
        FILE *out = fopen(path, "w");
        if (out) { gen_proto(out, prefix_lower); fclose(out); fprintf(stderr, "Generated %s\n", path); }
    }

    /* FlatBuffers */
    if (mode & OUT_FBS) {
        snprintf(path, sizeof(path), "%s/%s.fbs", outdir, prefix_lower);
        FILE *out = fopen(path, "w");
        if (out) { gen_fbs(out, prefix_lower); fclose(out); fprintf(stderr, "Generated %s\n", path); }
    }

    return 0;
}
