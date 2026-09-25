/* MBSE Stacks — API Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates C HTTP handler code from .api specifications.
 * Works with CivetWeb for HTTP serving.
 * Output is pure C with no runtime dependencies.
 *
 * TRUE DOGFOODING: Uses apigen_self.h which expands apigen_tokens.def
 * via X-macros to define this generator's own token types.
 *
 * Usage: apigen <service.api> [output_dir] [prefix]
 *
 * Spec format:
 *   api ServiceName {
 *       version: "1.0"
 *       transport: [http]
 *
 *       endpoint CreateUser {
 *           method: POST
 *           path: "/users"
 *           request: CreateUserRequest
 *           response: User
 *           errors: [InvalidInput, NotFound]
 *       }
 *
 *       type CreateUserRequest {
 *           name: string [not_empty]
 *           email: string
 *       }
 *
 *       type User {
 *           id: u64
 *           name: string
 *           email: string
 *       }
 *   }
 */

#include <stdio.h>
#include "../cosmo_harden.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

/* ── Self-hosted tokens (dogfooding) ─────────────────────────────── */
#include "apigen_self.h"

#define APIGEN_VERSION "1.0.0"
#define MAX_LINE 1024
#define MAX_ENDPOINTS 64
#define MAX_TYPES 64
#define MAX_FIELDS 32
#define MAX_ERRORS 16
#define MAX_NAME 64
#define MAX_PATH_LEN 128

typedef enum {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH
} http_method_t;

typedef struct {
    char name[MAX_NAME];
    char c_type[MAX_NAME];
    char constraint[MAX_NAME];
} field_t;

typedef struct {
    char name[MAX_NAME];
    field_t fields[MAX_FIELDS];
    int field_count;
} type_def_t;

typedef struct {
    char name[MAX_NAME];
    http_method_t method;
    char path[MAX_PATH_LEN];
    char request_type[MAX_NAME];
    char response_type[MAX_NAME];
    char handler_func[MAX_NAME];
    char errors[MAX_ERRORS][MAX_NAME];
    int error_count;
} endpoint_t;

typedef struct {
    char name[MAX_NAME];
    char version[MAX_NAME];
    endpoint_t endpoints[MAX_ENDPOINTS];
    int endpoint_count;
    type_def_t types[MAX_TYPES];
    int type_count;
} api_def_t;

static api_def_t api;

/* ── Utilities ────────────────────────────────────────────────────── */

static void trim(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
}

static void to_upper(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void to_lower(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void to_snake_case(const char *src, char *dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 1; i++) {
        char c = src[i];
        if (isupper((unsigned char)c)) {
            if (i > 0 && j > 0 && dst[j-1] != '_') {
                dst[j++] = '_';
            }
            dst[j++] = (char)tolower((unsigned char)c);
        } else if (isalnum((unsigned char)c)) {
            dst[j++] = c;
        } else if (c == ' ' || c == '-') {
            if (j > 0 && dst[j-1] != '_') {
                dst[j++] = '_';
            }
        }
    }
    dst[j] = '\0';
}

static const char *method_str(http_method_t m) {
    switch (m) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_PUT: return "PUT";
        case HTTP_DELETE: return "DELETE";
        case HTTP_PATCH: return "PATCH";
    }
    return "GET";
}

static http_method_t parse_method(const char *s) {
    if (strcmp(s, "GET") == 0) return HTTP_GET;
    if (strcmp(s, "POST") == 0) return HTTP_POST;
    if (strcmp(s, "PUT") == 0) return HTTP_PUT;
    if (strcmp(s, "DELETE") == 0) return HTTP_DELETE;
    if (strcmp(s, "PATCH") == 0) return HTTP_PATCH;
    return HTTP_GET;
}

static void extract_quoted(const char *line, char *out, size_t out_size) {
    const char *start = strchr(line, '"');
    if (!start) { out[0] = '\0'; return; }
    start++;
    const char *end = strchr(start, '"');
    if (!end) { out[0] = '\0'; return; }
    size_t len = (size_t)(end - start);
    if (len >= out_size) len = out_size - 1;
    strncpy(out, start, len);
    out[len] = '\0';
}

static void extract_value(const char *line, char *out, size_t out_size) {
    const char *p = strchr(line, ':');
    if (!p) { out[0] = '\0'; return; }
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    strncpy(out, p, out_size - 1);
    out[out_size - 1] = '\0';
    trim(out);
}

/* ── Parser ───────────────────────────────────────────────────────── */

static int parse_field(const char *line, type_def_t *t) {
    if (t->field_count >= MAX_FIELDS) return -1;

    field_t *f = &t->fields[t->field_count];
    memset(f, 0, sizeof(*f));

    /* Parse: name: type [constraint] */
    const char *colon = strchr(line, ':');
    if (!colon) return -1;

    int name_len = (int)(colon - line);
    if (name_len >= MAX_NAME) name_len = MAX_NAME - 1;
    strncpy(f->name, line, (size_t)name_len);
    f->name[name_len] = '\0';
    trim(f->name);

    const char *p = colon + 1;
    while (*p && isspace((unsigned char)*p)) p++;

    /* Check for constraint [xxx] */
    const char *bracket = strchr(p, '[');
    if (bracket) {
        int type_len = (int)(bracket - p);
        if (type_len >= MAX_NAME) type_len = MAX_NAME - 1;
        strncpy(f->c_type, p, (size_t)type_len);
        f->c_type[type_len] = '\0';
        trim(f->c_type);

        const char *end_bracket = strchr(bracket, ']');
        if (end_bracket) {
            int constraint_len = (int)(end_bracket - bracket - 1);
            if (constraint_len >= MAX_NAME) constraint_len = MAX_NAME - 1;
            strncpy(f->constraint, bracket + 1, (size_t)constraint_len);
            f->constraint[constraint_len] = '\0';
            trim(f->constraint);
        }
    } else {
        strncpy(f->c_type, p, MAX_NAME - 1);
        trim(f->c_type);
    }

    t->field_count++;
    return 0;
}

static void parse_errors(const char *line, endpoint_t *ep) {
    /* Parse: errors: [Error1, Error2] */
    const char *bracket = strchr(line, '[');
    if (!bracket) return;
    bracket++;

    char buf[MAX_LINE];
    strncpy(buf, bracket, sizeof(buf) - 1);
    char *end = strchr(buf, ']');
    if (end) *end = '\0';

    char *tok = strtok(buf, ",");
    while (tok && ep->error_count < MAX_ERRORS) {
        trim(tok);
        strncpy(ep->errors[ep->error_count++], tok, MAX_NAME - 1);
        tok = strtok(NULL, ",");
    }
}

static int parse_spec(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", filename);
        return -1;
    }

    memset(&api, 0, sizeof(api));

    char line[MAX_LINE];
    endpoint_t *cur_endpoint = NULL;
    type_def_t *cur_type = NULL;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        /* api Name { */
        if (strncmp(line, "api ", 4) == 0) {
            char *name_start = line + 4;
            while (*name_start && isspace((unsigned char)*name_start)) name_start++;
            char *brace = strchr(name_start, '{');
            if (brace) *brace = '\0';
            trim(name_start);
            strncpy(api.name, name_start, MAX_NAME - 1);
            continue;
        }

        /* version: "x.y" */
        if (strncmp(line, "version:", 8) == 0) {
            extract_quoted(line, api.version, MAX_NAME);
            continue;
        }

        /* endpoint Name { */
        if (strncmp(line, "endpoint ", 9) == 0) {
            if (api.endpoint_count >= MAX_ENDPOINTS) {
                fprintf(stderr, "Error: Too many endpoints\n");
                fclose(f);
                return -1;
            }
            cur_endpoint = &api.endpoints[api.endpoint_count++];
            memset(cur_endpoint, 0, sizeof(*cur_endpoint));
            cur_type = NULL;

            char *name_start = line + 9;
            while (*name_start && isspace((unsigned char)*name_start)) name_start++;
            char *brace = strchr(name_start, '{');
            if (brace) *brace = '\0';
            trim(name_start);
            strncpy(cur_endpoint->name, name_start, MAX_NAME - 1);

            /* Default handler name */
            to_snake_case(cur_endpoint->name, cur_endpoint->handler_func, MAX_NAME);
            continue;
        }

        /* type Name { */
        if (strncmp(line, "type ", 5) == 0) {
            if (api.type_count >= MAX_TYPES) {
                fprintf(stderr, "Error: Too many types\n");
                fclose(f);
                return -1;
            }
            cur_type = &api.types[api.type_count++];
            memset(cur_type, 0, sizeof(*cur_type));
            cur_endpoint = NULL;

            char *name_start = line + 5;
            while (*name_start && isspace((unsigned char)*name_start)) name_start++;
            char *brace = strchr(name_start, '{');
            if (brace) *brace = '\0';
            trim(name_start);
            strncpy(cur_type->name, name_start, MAX_NAME - 1);
            continue;
        }

        /* } - end block */
        if (line[0] == '}') {
            cur_endpoint = NULL;
            cur_type = NULL;
            continue;
        }

        /* Endpoint properties */
        if (cur_endpoint) {
            if (strncmp(line, "method:", 7) == 0) {
                char method[32];
                extract_value(line, method, sizeof(method));
                cur_endpoint->method = parse_method(method);
            } else if (strncmp(line, "path:", 5) == 0) {
                extract_quoted(line, cur_endpoint->path, MAX_PATH_LEN);
            } else if (strncmp(line, "request:", 8) == 0) {
                extract_value(line, cur_endpoint->request_type, MAX_NAME);
            } else if (strncmp(line, "response:", 9) == 0) {
                extract_value(line, cur_endpoint->response_type, MAX_NAME);
            } else if (strncmp(line, "handler:", 8) == 0) {
                extract_value(line, cur_endpoint->handler_func, MAX_NAME);
            } else if (strncmp(line, "errors:", 7) == 0) {
                parse_errors(line, cur_endpoint);
            }
            continue;
        }

        /* Type fields */
        if (cur_type) {
            if (strchr(line, ':')) {
                parse_field(line, cur_type);
            }
            continue;
        }
    }

    fclose(f);
    return 0;
}

/* ── Code Generation ──────────────────────────────────────────────── */

static void generate_header_guard(FILE *out, const char *guard) {
    fprintf(out, "/* AUTO-GENERATED by apigen %s — DO NOT EDIT */\n", APIGEN_VERSION);
    fprintf(out, "#ifndef %s\n", guard);
    fprintf(out, "#define %s\n\n", guard);
}

/* Store the current prefix for type lookup */
static const char *current_prefix = NULL;

static int is_custom_type(const char *spec_type) {
    /* Check if it's one of the API-defined types */
    for (int i = 0; i < api.type_count; i++) {
        if (strcmp(api.types[i].name, spec_type) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Return C type string - caller must use provided buffer for custom types */
static void api_c_type_buf(const char *spec_type, char *buf, size_t buf_size) {
    if (strcmp(spec_type, "string") == 0) { strncpy(buf, "char*", buf_size - 1); return; }
    if (strcmp(spec_type, "u8") == 0) { strncpy(buf, "uint8_t", buf_size - 1); return; }
    if (strcmp(spec_type, "u16") == 0) { strncpy(buf, "uint16_t", buf_size - 1); return; }
    if (strcmp(spec_type, "u32") == 0) { strncpy(buf, "uint32_t", buf_size - 1); return; }
    if (strcmp(spec_type, "u64") == 0) { strncpy(buf, "uint64_t", buf_size - 1); return; }
    if (strcmp(spec_type, "i8") == 0) { strncpy(buf, "int8_t", buf_size - 1); return; }
    if (strcmp(spec_type, "i16") == 0) { strncpy(buf, "int16_t", buf_size - 1); return; }
    if (strcmp(spec_type, "i32") == 0) { strncpy(buf, "int32_t", buf_size - 1); return; }
    if (strcmp(spec_type, "i64") == 0) { strncpy(buf, "int64_t", buf_size - 1); return; }
    if (strcmp(spec_type, "bool") == 0) { strncpy(buf, "int", buf_size - 1); return; }
    
    /* Custom type - add prefix and _t suffix */
    if (is_custom_type(spec_type) && current_prefix) {
        snprintf(buf, buf_size, "%s_%s_t", current_prefix, spec_type);
    } else {
        strncpy(buf, spec_type, buf_size - 1);
    }
    buf[buf_size - 1] = '\0';
}

static int generate_api_h(const char *outdir, const char *prefix) {
    char path[512];
    char guard[128];
    char header_name[128];
    char lower_prefix[32];

    strncpy(lower_prefix, prefix, sizeof(lower_prefix) - 1);
    lower_prefix[sizeof(lower_prefix) - 1] = '\0';
    to_lower(lower_prefix);

    snprintf(header_name, sizeof(header_name), "%s_api.h", lower_prefix);
    snprintf(guard, sizeof(guard), "%s_API_H", lower_prefix);
    to_upper(guard);

    snprintf(path, sizeof(path), "%s/%s", outdir, header_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    /* Set current prefix for type lookup */
    current_prefix = prefix;

    generate_header_guard(out, guard);

    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <stdbool.h>\n");
    fprintf(out, "#include <stddef.h>\n\n");

    /* API info */
    fprintf(out, "/* API: %s v%s */\n\n", api.name, api.version);

    /* Type definitions */
    fprintf(out, "/* Types */\n");
    for (int i = 0; i < api.type_count; i++) {
        type_def_t *t = &api.types[i];
        fprintf(out, "typedef struct {\n");
        for (int j = 0; j < t->field_count; j++) {
            field_t *f = &t->fields[j];
            char type_buf[128];
            api_c_type_buf(f->c_type, type_buf, sizeof(type_buf));
            fprintf(out, "    %s %s;\n", type_buf, f->name);
        }
        fprintf(out, "} %s_%s_t;\n\n", prefix, t->name);
    }

    /* Error codes - track unique errors to avoid duplicates */
    char seen_errors[MAX_ENDPOINTS * MAX_ERRORS][MAX_NAME];
    int seen_count = 0;

    fprintf(out, "/* Error codes */\n");
    fprintf(out, "typedef enum {\n");
    fprintf(out, "    %s_OK = 0,\n", prefix);
    fprintf(out, "    %s_ERR_INVALID_INPUT,\n", prefix);
    fprintf(out, "    %s_ERR_NOT_FOUND,\n", prefix);
    fprintf(out, "    %s_ERR_INTERNAL,\n", prefix);
    
    /* Add base errors to seen list */
    strncpy(seen_errors[seen_count++], "INVALID_INPUT", MAX_NAME - 1);
    strncpy(seen_errors[seen_count++], "NOT_FOUND", MAX_NAME - 1);
    strncpy(seen_errors[seen_count++], "INTERNAL", MAX_NAME - 1);
    
    /* Add custom errors from endpoints (deduplicated) */
    for (int i = 0; i < api.endpoint_count; i++) {
        endpoint_t *ep = &api.endpoints[i];
        for (int j = 0; j < ep->error_count; j++) {
            char upper[MAX_NAME];
            to_snake_case(ep->errors[j], upper, sizeof(upper));
            to_upper(upper);
            
            /* Check if already seen */
            int is_duplicate = 0;
            for (int k = 0; k < seen_count; k++) {
                if (strcmp(seen_errors[k], upper) == 0) {
                    is_duplicate = 1;
                    break;
                }
            }
            
            if (!is_duplicate && seen_count < MAX_ENDPOINTS * MAX_ERRORS) {
                strncpy(seen_errors[seen_count++], upper, MAX_NAME - 1);
                fprintf(out, "    %s_ERR_%s,\n", prefix, upper);
            }
        }
    }
    fprintf(out, "    %s_ERR_COUNT\n", prefix);
    fprintf(out, "} %s_error_t;\n\n", prefix);

    /* Request context */
    fprintf(out, "/* Request context */\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    const char *method;\n");
    fprintf(out, "    const char *path;\n");
    fprintf(out, "    const char *body;\n");
    fprintf(out, "    size_t body_len;\n");
    fprintf(out, "    void *user_data;\n");
    fprintf(out, "} %s_request_t;\n\n", prefix);

    /* Response context */
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    int status_code;\n");
    fprintf(out, "    char *body;\n");
    fprintf(out, "    size_t body_len;\n");
    fprintf(out, "    size_t body_cap;\n");
    fprintf(out, "} %s_response_t;\n\n", prefix);

    /* Handler prototype */
    fprintf(out, "/* Handler function type */\n");
    fprintf(out, "typedef %s_error_t (*%s_handler_fn)(%s_request_t *req, %s_response_t *resp);\n\n",
            prefix, prefix, prefix, prefix);

    /* Route entry */
    fprintf(out, "/* Route table entry */\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    const char *method;\n");
    fprintf(out, "    const char *path;\n");
    fprintf(out, "    %s_handler_fn handler;\n", prefix);
    fprintf(out, "} %s_route_t;\n\n", prefix);

    /* Function declarations */
    fprintf(out, "/* API functions */\n");
    fprintf(out, "void %s_init(void);\n", prefix);
    fprintf(out, "%s_error_t %s_dispatch(%s_request_t *req, %s_response_t *resp);\n", prefix, prefix, prefix, prefix);
    fprintf(out, "const %s_route_t *%s_get_routes(int *count);\n", prefix, prefix);
    fprintf(out, "const char *%s_error_str(%s_error_t err);\n\n", prefix, prefix);

    /* Handler prototypes */
    fprintf(out, "/* Endpoint handlers (implement these) */\n");
    for (int i = 0; i < api.endpoint_count; i++) {
        endpoint_t *ep = &api.endpoints[i];
        fprintf(out, "%s_error_t %s_%s(%s_request_t *req, %s_response_t *resp);\n",
                prefix, prefix, ep->handler_func, prefix, prefix);
    }
    fprintf(out, "\n");

    fprintf(out, "#endif /* %s */\n", guard);
    fclose(out);

    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_api_c(const char *outdir, const char *prefix) {
    char path[512];
    char header_name[128];
    char impl_name[128];
    char lower_prefix[32];

    strncpy(lower_prefix, prefix, sizeof(lower_prefix) - 1);
    lower_prefix[sizeof(lower_prefix) - 1] = '\0';
    to_lower(lower_prefix);

    snprintf(header_name, sizeof(header_name), "%s_api.h", lower_prefix);
    snprintf(impl_name, sizeof(impl_name), "%s_api.c", lower_prefix);

    snprintf(path, sizeof(path), "%s/%s", outdir, impl_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    fprintf(out, "/* AUTO-GENERATED by apigen %s — DO NOT EDIT */\n\n", APIGEN_VERSION);
    fprintf(out, "#include \"%s\"\n", header_name);
    fprintf(out, "#include <string.h>\n\n");

    /* Error strings */
    fprintf(out, "static const char *error_strings[] = {\n");
    fprintf(out, "    \"OK\",\n");
    fprintf(out, "    \"Invalid input\",\n");
    fprintf(out, "    \"Not found\",\n");
    fprintf(out, "    \"Internal error\",\n");
    for (int i = 0; i < api.endpoint_count; i++) {
        endpoint_t *ep = &api.endpoints[i];
        for (int j = 0; j < ep->error_count; j++) {
            fprintf(out, "    \"%s\",\n", ep->errors[j]);
        }
    }
    fprintf(out, "};\n\n");

    /* Route table */
    fprintf(out, "static const %s_route_t routes[] = {\n", prefix);
    for (int i = 0; i < api.endpoint_count; i++) {
        endpoint_t *ep = &api.endpoints[i];
        fprintf(out, "    {\"%s\", \"%s\", %s_%s},\n",
                method_str(ep->method), ep->path, prefix, ep->handler_func);
    }
    fprintf(out, "};\n");
    fprintf(out, "static const int route_count = %d;\n\n", api.endpoint_count);

    /* Init */
    fprintf(out, "void %s_init(void) {\n", prefix);
    fprintf(out, "    /* API initialization */\n");
    fprintf(out, "}\n\n");

    /* Error string */
    fprintf(out, "const char *%s_error_str(%s_error_t err) {\n", prefix, prefix);
    fprintf(out, "    if (err >= 0 && err < %s_ERR_COUNT) {\n", prefix);
    fprintf(out, "        return error_strings[err];\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return \"Unknown error\";\n");
    fprintf(out, "}\n\n");

    /* Get routes */
    fprintf(out, "const %s_route_t *%s_get_routes(int *count) {\n", prefix, prefix);
    fprintf(out, "    *count = route_count;\n");
    fprintf(out, "    return routes;\n");
    fprintf(out, "}\n\n");

    /* Dispatcher */
    fprintf(out, "%s_error_t %s_dispatch(%s_request_t *req, %s_response_t *resp) {\n",
            prefix, prefix, prefix, prefix);
    fprintf(out, "    for (int i = 0; i < route_count; i++) {\n");
    fprintf(out, "        if (strcmp(routes[i].method, req->method) == 0 &&\n");
    fprintf(out, "            strcmp(routes[i].path, req->path) == 0) {\n");
    fprintf(out, "            return routes[i].handler(req, resp);\n");
    fprintf(out, "        }\n");
    fprintf(out, "    }\n");
    fprintf(out, "    resp->status_code = 404;\n");
    fprintf(out, "    return %s_ERR_NOT_FOUND;\n", prefix);
    fprintf(out, "}\n");

    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static void generate_version(const char *outdir, const char *profile) {
    char path[512];
    snprintf(path, sizeof(path), "%s/GENERATOR_VERSION", outdir);

    FILE *out = fopen(path, "w");
    if (!out) return;

    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    fprintf(out, "apigen %s\n", APIGEN_VERSION);
    fprintf(out, "generated: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
    fprintf(out, "profile: %s\n", profile);
    fprintf(out, "api: %s\n", api.name);
    fprintf(out, "version: %s\n", api.version);
    fprintf(out, "endpoints: %d\n", api.endpoint_count);
    fprintf(out, "types: %d\n", api.type_count);

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
    fprintf(stderr, "apigen %s — REST API Generator\n", APIGEN_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: apigen <service.api> [output_dir] [prefix]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Generates REST API handlers with:\n");
    fprintf(stderr, "  - Route table with method/path matching\n");
    fprintf(stderr, "  - Request/response type definitions\n");
    fprintf(stderr, "  - Error code enumeration\n");
    fprintf(stderr, "  - Dispatcher function\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Spec format:\n");
    fprintf(stderr, "  api Name {\n");
    fprintf(stderr, "      version: \"1.0\"\n");
    fprintf(stderr, "      endpoint GetUser {\n");
    fprintf(stderr, "          method: GET\n");
    fprintf(stderr, "          path: \"/users/{id}\"\n");
    fprintf(stderr, "          response: User\n");
    fprintf(stderr, "      }\n");
    fprintf(stderr, "      type User { id: u64, name: string }\n");
    fprintf(stderr, "  }\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Output:\n");
    fprintf(stderr, "  <prefix>_api.h  — Types, routes, handler prototypes\n");
    fprintf(stderr, "  <prefix>_api.c  — Dispatcher and route table\n");
}

int main(int argc, char *argv[]) {
    tool_harden();
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *input = argv[1];
    const char *outdir = argc > 2 ? argv[2] : ".";
    const char *profile = getenv("PROFILE");
    if (!profile) profile = "portable";

    if (parse_spec(input) != 0) {
        return 1;
    }

    /* Use api name as default prefix */
    const char *prefix = argc > 3 ? argv[3] : api.name;
    if (!prefix[0]) prefix = "API";

    fprintf(stderr, "Parsed API '%s' v%s: %d endpoints, %d types\n",
            api.name, api.version, api.endpoint_count, api.type_count);

    if (ensure_output_dir(outdir) != 0) {
        return 1;
    }

    if (generate_api_h(outdir, prefix) != 0) return 1;
    if (generate_api_c(outdir, prefix) != 0) return 1;

    generate_version(outdir, profile);

    return 0;
}
