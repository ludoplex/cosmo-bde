/* OpenSmith Front-End Generator
 * Ring 0: Pure C parser/AST frontend for template parity work.
 *
 * This tool parses OpenSmith-style template syntax and can:
 *   - emit AST JSON
 *   - emit roundtripped template text
 *   - verify exact parse->roundtrip stability
 *
 * Supported tag forms:
 *   <%@ Directive Key="Value" %>
 *   <%= expression %>
 *   <% statement %>
 *   <%-- comment --%>
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENSMITHGEN_VERSION "0.2.0"

typedef enum {
    NODE_LITERAL = 0,
    NODE_DIRECTIVE,
    NODE_EXPRESSION,
    NODE_STATEMENT,
    NODE_COMMENT
} node_kind_t;

typedef struct {
    char *key;
    char *value;
} attr_t;

typedef struct {
    node_kind_t kind;
    size_t start;
    size_t end;
    int line;
    int col;
    char *raw;
    char *inner;
    char *body;
    char *directive_name;
    attr_t *attrs;
    size_t attr_count;
    size_t attr_cap;
} ast_node_t;

typedef struct {
    ast_node_t *nodes;
    size_t count;
    size_t cap;
} ast_doc_t;

typedef struct {
    int line;
    int col;
    char msg[160];
} parse_error_t;

typedef enum {
    MODE_AST = 0,
    MODE_ROUNDTRIP,
    MODE_CHECK_ROUNDTRIP,
    MODE_STATS
} opensmith_mode_t;

static void usage(const char *argv0) {
    fprintf(stderr,
            "OpenSmith Front-End Generator %s\n"
            "Usage: %s [--ast|--roundtrip|--check-roundtrip|--stats] <input.cst>\n"
            "\n"
            "Modes:\n"
            "  --ast              Emit AST JSON (default)\n"
            "  --roundtrip        Emit reconstructed template\n"
            "  --check-roundtrip  Validate exact parse->roundtrip stability\n"
            "  --stats            Print node statistics\n",
            OPENSMITHGEN_VERSION,
            argv0);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(1);
    }
    return p;
}

static void *xrealloc(void *ptr, size_t n) {
    void *p = realloc(ptr, n);
    if (!p) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(1);
    }
    return p;
}

static char *xstrndup0(const char *s, size_t n) {
    char *d = (char *)xmalloc(n + 1);
    if (n > 0) {
        memcpy(d, s, n);
    }
    d[n] = '\0';
    return d;
}

static char *dup_trimmed(const char *s) {
    size_t a = 0;
    size_t b = strlen(s);
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return xstrndup0(s + a, b - a);
}

static int is_ident_char(int c) {
    unsigned char uc = (unsigned char)c;
    return isalnum(uc) || uc == '_' || uc == '-' || uc == '.' || uc == ':';
}

static const char *node_kind_str(node_kind_t kind) {
    switch (kind) {
        case NODE_LITERAL:
            return "literal";
        case NODE_DIRECTIVE:
            return "directive";
        case NODE_EXPRESSION:
            return "expression";
        case NODE_STATEMENT:
            return "statement";
        case NODE_COMMENT:
            return "comment";
        default:
            return "unknown";
    }
}

static const char *directive_role(const char *name) {
    char lower[64];
    size_t i;
    size_t n;
    if (!name || !name[0]) return "unknown";

    n = strlen(name);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (i = 0; i < n; i++) {
        lower[i] = (char)tolower((unsigned char)name[i]);
    }
    lower[n] = '\0';

    if (strcmp(lower, "template") == 0) return "template";
    if (strcmp(lower, "property") == 0) return "property";
    if (strcmp(lower, "include") == 0) return "include";
    if (strcmp(lower, "master") == 0) return "master";
    if (strcmp(lower, "assembly") == 0) return "assembly";
    if (strcmp(lower, "import") == 0) return "import";
    if (strcmp(lower, "reference") == 0) return "reference";
    return "other";
}

static void free_node(ast_node_t *n) {
    size_t i;
    if (!n) return;
    free(n->raw);
    free(n->inner);
    free(n->body);
    free(n->directive_name);
    for (i = 0; i < n->attr_count; i++) {
        free(n->attrs[i].key);
        free(n->attrs[i].value);
    }
    free(n->attrs);
    memset(n, 0, sizeof(*n));
}

static void free_doc(ast_doc_t *doc) {
    size_t i;
    if (!doc) return;
    for (i = 0; i < doc->count; i++) {
        free_node(&doc->nodes[i]);
    }
    free(doc->nodes);
    memset(doc, 0, sizeof(*doc));
}

static int add_attr(ast_node_t *node, char *key, char *value) {
    if (node->attr_count == node->attr_cap) {
        size_t new_cap = (node->attr_cap == 0) ? 4 : node->attr_cap * 2;
        attr_t *new_attrs = (attr_t *)xrealloc(node->attrs, new_cap * sizeof(*new_attrs));
        node->attrs = new_attrs;
        node->attr_cap = new_cap;
    }
    node->attrs[node->attr_count].key = key;
    node->attrs[node->attr_count].value = value;
    node->attr_count++;
    return 0;
}

static int add_node(ast_doc_t *doc, ast_node_t *node) {
    if (doc->count == doc->cap) {
        size_t new_cap = (doc->cap == 0) ? 64 : doc->cap * 2;
        ast_node_t *new_nodes = (ast_node_t *)xrealloc(doc->nodes, new_cap * sizeof(*new_nodes));
        doc->nodes = new_nodes;
        doc->cap = new_cap;
    }
    doc->nodes[doc->count] = *node;
    doc->count++;
    memset(node, 0, sizeof(*node));
    return 0;
}

static void advance_pos(const char *buf, size_t start, size_t end, int *line, int *col) {
    size_t i;
    for (i = start; i < end; i++) {
        if (buf[i] == '\n') {
            (*line)++;
            *col = 1;
        } else {
            (*col)++;
        }
    }
}

static size_t find_token(const char *buf, size_t start, size_t len, const char *tok) {
    size_t tok_len = strlen(tok);
    size_t i;
    if (tok_len == 0 || start >= len) return (size_t)-1;
    if (tok_len > len - start) return (size_t)-1;

    for (i = start; i + tok_len <= len; i++) {
        if (memcmp(buf + i, tok, tok_len) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static void parse_directive_attrs(ast_node_t *node) {
    const char *s = node->inner;
    size_t n = strlen(s);
    size_t i = 0;

    while (i < n && isspace((unsigned char)s[i])) i++;
    if (i < n && s[i] == '@') i++;
    while (i < n && isspace((unsigned char)s[i])) i++;

    {
        size_t name_start = i;
        while (i < n && is_ident_char((unsigned char)s[i])) i++;
        if (i > name_start) {
            node->directive_name = xstrndup0(s + name_start, i - name_start);
        } else {
            node->directive_name = xstrndup0("", 0);
        }
    }

    while (i < n) {
        size_t key_start;
        size_t key_end;
        char *key;
        char *value;

        while (i < n && isspace((unsigned char)s[i])) i++;
        if (i >= n) break;

        key_start = i;
        while (i < n && is_ident_char((unsigned char)s[i])) i++;
        key_end = i;

        if (key_end == key_start) {
            i++;
            continue;
        }

        key = xstrndup0(s + key_start, key_end - key_start);
        while (i < n && isspace((unsigned char)s[i])) i++;

        if (i < n && s[i] == '=') {
            i++;
            while (i < n && isspace((unsigned char)s[i])) i++;

            if (i < n && (s[i] == '"' || s[i] == '\'')) {
                char quote = s[i++];
                size_t vstart = i;
                while (i < n && s[i] != quote) {
                    if (s[i] == '\\' && i + 1 < n) {
                        i += 2;
                    } else {
                        i++;
                    }
                }
                value = xstrndup0(s + vstart, i - vstart);
                if (i < n && s[i] == quote) i++;
            } else {
                size_t vstart = i;
                while (i < n && !isspace((unsigned char)s[i])) i++;
                value = xstrndup0(s + vstart, i - vstart);
            }
        } else {
            value = xstrndup0("", 0);
        }

        add_attr(node, key, value);
    }
}

static void classify_node(ast_node_t *node) {
    char *trimmed;
    size_t tlen;

    if (!node->inner) {
        node->kind = NODE_STATEMENT;
        node->body = xstrndup0("", 0);
        return;
    }

    trimmed = dup_trimmed(node->inner);
    tlen = strlen(trimmed);

    if (tlen >= 2 && trimmed[0] == '-' && trimmed[1] == '-') {
        node->kind = NODE_COMMENT;
        node->body = dup_trimmed(trimmed + 2);
        free(trimmed);
        return;
    }

    if (tlen > 0 && trimmed[0] == '@') {
        node->kind = NODE_DIRECTIVE;
        node->body = dup_trimmed(trimmed + 1);
        parse_directive_attrs(node);
        free(trimmed);
        return;
    }

    if (tlen > 0 && trimmed[0] == '=') {
        node->kind = NODE_EXPRESSION;
        node->body = dup_trimmed(trimmed + 1);
        free(trimmed);
        return;
    }

    node->kind = NODE_STATEMENT;
    node->body = trimmed;
}

static int parse_template(const char *buf, size_t len, ast_doc_t *doc, parse_error_t *err) {
    size_t i = 0;
    int line = 1;
    int col = 1;

    while (i < len) {
        size_t tag_start = find_token(buf, i, len, "<%");
        if (tag_start == (size_t)-1) {
            if (tag_start != i) {
                ast_node_t lit;
                memset(&lit, 0, sizeof(lit));
                lit.kind = NODE_LITERAL;
                lit.start = i;
                lit.end = len;
                lit.line = line;
                lit.col = col;
                lit.raw = xstrndup0(buf + i, len - i);
                lit.body = xstrndup0(lit.raw, strlen(lit.raw));
                add_node(doc, &lit);
                advance_pos(buf, i, len, &line, &col);
            }
            break;
        }

        if (tag_start > i) {
            ast_node_t lit;
            memset(&lit, 0, sizeof(lit));
            lit.kind = NODE_LITERAL;
            lit.start = i;
            lit.end = tag_start;
            lit.line = line;
            lit.col = col;
            lit.raw = xstrndup0(buf + i, tag_start - i);
            lit.body = xstrndup0(lit.raw, strlen(lit.raw));
            add_node(doc, &lit);
            advance_pos(buf, i, tag_start, &line, &col);
            i = tag_start;
        }

        {
            size_t close = find_token(buf, i + 2, len, "%>");
            size_t end;
            ast_node_t node;

            if (close == (size_t)-1) {
                err->line = line;
                err->col = col;
                snprintf(err->msg, sizeof(err->msg), "unterminated tag; missing %%>");
                return -1;
            }

            end = close + 2;
            memset(&node, 0, sizeof(node));
            node.start = i;
            node.end = end;
            node.line = line;
            node.col = col;
            node.raw = xstrndup0(buf + i, end - i);
            node.inner = xstrndup0(buf + i + 2, close - (i + 2));

            classify_node(&node);
            add_node(doc, &node);

            advance_pos(buf, i, end, &line, &col);
            i = end;
        }
    }

    return 0;
}

static int read_file(const char *path, char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    char *buf;
    size_t cap = 8192;
    size_t len = 0;

    if (!f) {
        fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    buf = (char *)xmalloc(cap + 1);
    while (!feof(f)) {
        size_t n;
        if (len == cap) {
            cap *= 2;
            buf = (char *)xrealloc(buf, cap + 1);
        }
        n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (ferror(f)) {
            fprintf(stderr, "error: failed reading %s\n", path);
            fclose(f);
            free(buf);
            return -1;
        }
    }

    fclose(f);
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return 0;
}

static void json_escape(FILE *out, const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    fputc('"', out);
    while (*p) {
        switch (*p) {
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
                if (*p < 0x20) {
                    fprintf(out, "\\u%04x", (unsigned int)*p);
                } else {
                    fputc((int)*p, out);
                }
                break;
        }
        p++;
    }
    fputc('"', out);
}

static void emit_ast_json(FILE *out, const char *path, const ast_doc_t *doc) {
    size_t i;
    fprintf(out, "{\n");
    fprintf(out, "  \"file\": ");
    json_escape(out, path);
    fprintf(out, ",\n");
    fprintf(out, "  \"node_count\": %lu,\n", (unsigned long)doc->count);
    fprintf(out, "  \"nodes\": [\n");

    for (i = 0; i < doc->count; i++) {
        const ast_node_t *n = &doc->nodes[i];
        size_t j;
        fprintf(out, "    {\n");
        fprintf(out, "      \"kind\": ");
        json_escape(out, node_kind_str(n->kind));
        fprintf(out, ",\n");
        fprintf(out, "      \"offset\": %lu,\n", (unsigned long)n->start);
        fprintf(out, "      \"length\": %lu,\n", (unsigned long)(n->end - n->start));
        fprintf(out, "      \"line\": %d,\n", n->line);
        fprintf(out, "      \"column\": %d,\n", n->col);
        fprintf(out, "      \"raw\": ");
        json_escape(out, n->raw ? n->raw : "");

        if (n->kind != NODE_LITERAL) {
            fprintf(out, ",\n      \"inner\": ");
            json_escape(out, n->inner ? n->inner : "");
            fprintf(out, ",\n      \"body\": ");
            json_escape(out, n->body ? n->body : "");
        }

        if (n->kind == NODE_DIRECTIVE) {
            fprintf(out, ",\n      \"directive\": {\n");
            fprintf(out, "        \"name\": ");
            json_escape(out, n->directive_name ? n->directive_name : "");
            fprintf(out, ",\n        \"role\": ");
            json_escape(out, directive_role(n->directive_name));
            fprintf(out, ",\n        \"attributes\": [");
            for (j = 0; j < n->attr_count; j++) {
                if (j == 0) fprintf(out, "\n");
                fprintf(out, "          {\"key\": ");
                json_escape(out, n->attrs[j].key ? n->attrs[j].key : "");
                fprintf(out, ", \"value\": ");
                json_escape(out, n->attrs[j].value ? n->attrs[j].value : "");
                fprintf(out, "}");
                if (j + 1 < n->attr_count) fprintf(out, ",");
                fprintf(out, "\n");
            }
            if (n->attr_count > 0) {
                fprintf(out, "        ]\n");
            } else {
                fprintf(out, "]\n");
            }
            fprintf(out, "      }");
        }

        fprintf(out, "\n    }");
        if (i + 1 < doc->count) fprintf(out, ",");
        fprintf(out, "\n");
    }

    fprintf(out, "  ]\n");
    fprintf(out, "}\n");
}

static int reconstruct(const ast_doc_t *doc, char **out, size_t *out_len) {
    size_t i;
    size_t total = 0;
    size_t off = 0;
    char *buf;

    for (i = 0; i < doc->count; i++) {
        const ast_node_t *n = &doc->nodes[i];
        size_t raw_len = n->raw ? strlen(n->raw) : 0;
        if (raw_len > ((size_t)-1) - total - 1) {
            fprintf(stderr, "error: output too large\n");
            return -1;
        }
        total += raw_len;
    }

    buf = (char *)xmalloc(total + 1);
    for (i = 0; i < doc->count; i++) {
        const ast_node_t *n = &doc->nodes[i];
        size_t raw_len = n->raw ? strlen(n->raw) : 0;
        if (raw_len > 0) {
            memcpy(buf + off, n->raw, raw_len);
            off += raw_len;
        }
    }
    buf[off] = '\0';
    *out = buf;
    *out_len = off;
    return 0;
}

static int check_roundtrip(const char *orig, size_t orig_len, const ast_doc_t *doc) {
    char *rt = NULL;
    size_t rt_len = 0;
    size_t i;

    if (reconstruct(doc, &rt, &rt_len) != 0) {
        return 1;
    }

    if (rt_len != orig_len || memcmp(rt, orig, orig_len) != 0) {
        size_t min_len = (rt_len < orig_len) ? rt_len : orig_len;
        for (i = 0; i < min_len; i++) {
            if (rt[i] != orig[i]) {
                fprintf(stderr,
                        "roundtrip mismatch at byte %lu (expected 0x%02x, got 0x%02x)\n",
                        (unsigned long)i,
                        (unsigned int)(unsigned char)orig[i],
                        (unsigned int)(unsigned char)rt[i]);
                free(rt);
                return 1;
            }
        }

        fprintf(stderr,
                "roundtrip length mismatch (expected %lu, got %lu)\n",
                (unsigned long)orig_len,
                (unsigned long)rt_len);
        free(rt);
        return 1;
    }

    free(rt);
    return 0;
}

static void emit_stats(FILE *out, const ast_doc_t *doc) {
    size_t i;
    size_t literals = 0;
    size_t directives = 0;
    size_t expressions = 0;
    size_t statements = 0;
    size_t comments = 0;

    for (i = 0; i < doc->count; i++) {
        switch (doc->nodes[i].kind) {
            case NODE_LITERAL:
                literals++;
                break;
            case NODE_DIRECTIVE:
                directives++;
                break;
            case NODE_EXPRESSION:
                expressions++;
                break;
            case NODE_STATEMENT:
                statements++;
                break;
            case NODE_COMMENT:
                comments++;
                break;
            default:
                break;
        }
    }

    fprintf(out, "nodes=%lu literals=%lu directives=%lu expressions=%lu statements=%lu comments=%lu\n",
            (unsigned long)doc->count,
            (unsigned long)literals,
            (unsigned long)directives,
            (unsigned long)expressions,
            (unsigned long)statements,
            (unsigned long)comments);
}

int main(int argc, char **argv) {
    opensmith_mode_t mode = MODE_AST;
    const char *input = NULL;
    char *src = NULL;
    size_t src_len = 0;
    ast_doc_t doc;
    parse_error_t err;
    int rc = 0;
    int i;

    memset(&doc, 0, sizeof(doc));
    memset(&err, 0, sizeof(err));

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--ast") == 0) {
            mode = MODE_AST;
        } else if (strcmp(argv[i], "--roundtrip") == 0) {
            mode = MODE_ROUNDTRIP;
        } else if (strcmp(argv[i], "--check-roundtrip") == 0) {
            mode = MODE_CHECK_ROUNDTRIP;
        } else if (strcmp(argv[i], "--stats") == 0) {
            mode = MODE_STATS;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else {
            input = argv[i];
        }
    }

    if (!input) {
        fprintf(stderr, "error: missing input path\n");
        usage(argv[0]);
        return 2;
    }

    if (read_file(input, &src, &src_len) != 0) {
        return 1;
    }

    if (parse_template(src, src_len, &doc, &err) != 0) {
        fprintf(stderr,
                "parse error in %s at line %d, column %d: %s\n",
                input,
                err.line,
                err.col,
                err.msg);
        free(src);
        free_doc(&doc);
        return 1;
    }

    if (mode == MODE_AST) {
        emit_ast_json(stdout, input, &doc);
    } else if (mode == MODE_ROUNDTRIP) {
        char *rt = NULL;
        size_t rt_len = 0;
        if (reconstruct(&doc, &rt, &rt_len) != 0) {
            rc = 1;
        } else {
            if (rt_len > 0) {
                fwrite(rt, 1, rt_len, stdout);
            }
            free(rt);
        }
    } else if (mode == MODE_CHECK_ROUNDTRIP) {
        rc = check_roundtrip(src, src_len, &doc);
        if (rc == 0) {
            fprintf(stderr, "OK %s (%lu nodes)\n", input, (unsigned long)doc.count);
        }
    } else if (mode == MODE_STATS) {
        emit_stats(stdout, &doc);
    }

    free(src);
    free_doc(&doc);
    return rc;
}
