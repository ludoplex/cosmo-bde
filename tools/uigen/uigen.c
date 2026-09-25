/* MBSE Stacks — UI Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates Nuklear-based UI code from .ui specifications.
 * Output is pure C with data bindings and event handlers.
 *
 * TRUE DOGFOODING: Uses uigen_self.h which expands uigen_tokens.def
 * via X-macros to define this generator's own token types.
 *
 * Usage: uigen <interface.ui> [output_dir] [prefix]
 *
 * Spec format:
 *   window WindowName {
 *       title: "Window Title"
 *       width: 800
 *       height: 600
 *
 *       panel PanelName {
 *           layout: horizontal | vertical | grid
 *
 *           button ButtonName {
 *               label: "Click Me"
 *               on_click: handler_func
 *           }
 *
 *           slider SliderName {
 *               min: 0
 *               max: 100
 *               bind: model.field
 *           }
 *
 *           label LabelName {
 *               bind: model.status
 *           }
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
#include "uigen_self.h"

#define UIGEN_VERSION "1.0.0"
#define MAX_LINE 1024
#define MAX_WINDOWS 16
#define MAX_PANELS 64
#define MAX_WIDGETS 256
#define MAX_NAME 64
#define MAX_PATH 512

typedef enum {
    WIDGET_BUTTON,
    WIDGET_LABEL,
    WIDGET_SLIDER,
    WIDGET_CHECKBOX,
    WIDGET_TEXTBOX,
    WIDGET_COMBO,
} widget_type_t;

typedef enum {
    LAYOUT_VERTICAL,
    LAYOUT_HORIZONTAL,
    LAYOUT_GRID,
} layout_t;

typedef struct {
    char name[MAX_NAME];
    widget_type_t type;
    char label[MAX_NAME];
    char bind[MAX_NAME];
    char on_click[MAX_NAME];
    char on_change[MAX_NAME];
    int min_val, max_val;
    int parent_panel;
} widget_t;

typedef struct {
    char name[MAX_NAME];
    layout_t layout;
    int parent_window;
    int widget_start;
    int widget_count;
} panel_t;

typedef struct {
    char name[MAX_NAME];
    char title[MAX_NAME];
    int width, height;
    int panel_start;
    int panel_count;
} window_t;

static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static panel_t panels[MAX_PANELS];
static int panel_count = 0;
static widget_t widgets[MAX_WIDGETS];
static int widget_count = 0;

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

static int extract_int(const char *line) {
    const char *p = strchr(line, ':');
    if (!p) return 0;
    return atoi(p + 1);
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

static widget_type_t parse_widget_type(const char *type) {
    if (strcmp(type, "button") == 0) return WIDGET_BUTTON;
    if (strcmp(type, "label") == 0) return WIDGET_LABEL;
    if (strcmp(type, "slider") == 0) return WIDGET_SLIDER;
    if (strcmp(type, "checkbox") == 0) return WIDGET_CHECKBOX;
    if (strcmp(type, "textbox") == 0) return WIDGET_TEXTBOX;
    if (strcmp(type, "combo") == 0) return WIDGET_COMBO;
    return WIDGET_LABEL;
}

static layout_t parse_layout(const char *layout) {
    if (strcmp(layout, "horizontal") == 0) return LAYOUT_HORIZONTAL;
    if (strcmp(layout, "grid") == 0) return LAYOUT_GRID;
    return LAYOUT_VERTICAL;
}

static int parse_spec(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", filename);
        return -1;
    }

    char line[MAX_LINE];
    window_t *cur_window = NULL;
    panel_t *cur_panel = NULL;
    widget_t *cur_widget = NULL;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        /* window Name { */
        if (strncmp(line, "window ", 7) == 0) {
            if (window_count >= MAX_WINDOWS) {
                fprintf(stderr, "Error: Too many windows\n");
                fclose(f);
                return -1;
            }
            cur_window = &windows[window_count++];
            memset(cur_window, 0, sizeof(*cur_window));
            cur_window->panel_start = panel_count;
            cur_window->width = 800;
            cur_window->height = 600;

            char *name = line + 7;
            while (*name && isspace((unsigned char)*name)) name++;
            char *brace = strchr(name, '{');
            if (brace) *brace = '\0';
            trim(name);
            strncpy(cur_window->name, name, MAX_NAME - 1);
            continue;
        }

        /* panel Name { */
        if (strncmp(line, "panel ", 6) == 0) {
            if (panel_count >= MAX_PANELS) {
                fprintf(stderr, "Error: Too many panels\n");
                fclose(f);
                return -1;
            }
            cur_panel = &panels[panel_count++];
            memset(cur_panel, 0, sizeof(*cur_panel));
            cur_panel->parent_window = window_count - 1;
            cur_panel->widget_start = widget_count;
            if (cur_window) cur_window->panel_count++;

            char *name = line + 6;
            while (*name && isspace((unsigned char)*name)) name++;
            char *brace = strchr(name, '{');
            if (brace) *brace = '\0';
            trim(name);
            strncpy(cur_panel->name, name, MAX_NAME - 1);
            continue;
        }

        /* widget Type Name { */
        const char *widget_types[] = {"button", "label", "slider", "checkbox", "textbox", "combo"};
        for (int i = 0; i < 6; i++) {
            size_t len = strlen(widget_types[i]);
            if (strncmp(line, widget_types[i], len) == 0 && isspace((unsigned char)line[len])) {
                if (widget_count >= MAX_WIDGETS) {
                    fprintf(stderr, "Error: Too many widgets\n");
                    fclose(f);
                    return -1;
                }
                cur_widget = &widgets[widget_count++];
                memset(cur_widget, 0, sizeof(*cur_widget));
                cur_widget->type = parse_widget_type(widget_types[i]);
                cur_widget->parent_panel = panel_count - 1;
                if (cur_panel) cur_panel->widget_count++;

                char *name = line + len;
                while (*name && isspace((unsigned char)*name)) name++;
                char *brace = strchr(name, '{');
                if (brace) *brace = '\0';
                trim(name);
                strncpy(cur_widget->name, name, MAX_NAME - 1);
                break;
            }
        }

        /* Properties */
        if (strncmp(line, "title:", 6) == 0 && cur_window) {
            extract_quoted(line, cur_window->title, MAX_NAME);
        } else if (strncmp(line, "width:", 6) == 0 && cur_window) {
            cur_window->width = extract_int(line);
        } else if (strncmp(line, "height:", 7) == 0 && cur_window) {
            cur_window->height = extract_int(line);
        } else if (strncmp(line, "layout:", 7) == 0 && cur_panel) {
            char layout[MAX_NAME];
            extract_value(line, layout, MAX_NAME);
            cur_panel->layout = parse_layout(layout);
        } else if (strncmp(line, "label:", 6) == 0 && cur_widget) {
            extract_quoted(line, cur_widget->label, MAX_NAME);
        } else if (strncmp(line, "bind:", 5) == 0 && cur_widget) {
            extract_value(line, cur_widget->bind, MAX_NAME);
        } else if (strncmp(line, "on_click:", 9) == 0 && cur_widget) {
            extract_value(line, cur_widget->on_click, MAX_NAME);
        } else if (strncmp(line, "on_change:", 10) == 0 && cur_widget) {
            extract_value(line, cur_widget->on_change, MAX_NAME);
        } else if (strncmp(line, "min:", 4) == 0 && cur_widget) {
            cur_widget->min_val = extract_int(line);
        } else if (strncmp(line, "max:", 4) == 0 && cur_widget) {
            cur_widget->max_val = extract_int(line);
        }

        /* } - close block */
        if (line[0] == '}') {
            if (cur_widget) {
                cur_widget = NULL;
            } else if (cur_panel) {
                cur_panel = NULL;
            } else if (cur_window) {
                cur_window = NULL;
            }
        }
    }

    fclose(f);
    return 0;
}

/* ── Code Generation ──────────────────────────────────────────────── */

static void generate_header_guard(FILE *out, const char *guard) {
    fprintf(out, "/* AUTO-GENERATED by uigen %s — DO NOT EDIT */\n", UIGEN_VERSION);
    fprintf(out, "#ifndef %s\n", guard);
    fprintf(out, "#define %s\n\n", guard);
}

static int generate_ui_h(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char guard[128];
    char header_name[128];
    char lower_prefix[MAX_NAME];

    strncpy(lower_prefix, prefix, MAX_NAME - 1);
    to_lower(lower_prefix);

    snprintf(header_name, sizeof(header_name), "%s_ui.h", lower_prefix);
    snprintf(guard, sizeof(guard), "%s_UI_H", prefix);
    to_upper(guard);

    snprintf(path, sizeof(path), "%s/%s", outdir, header_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    generate_header_guard(out, guard);
    fprintf(out, "#include <stdbool.h>\n\n");
    fprintf(out, "/* Forward declaration for Nuklear context */\n");
    fprintf(out, "struct nk_context;\n\n");

    /* Window IDs */
    fprintf(out, "/* Window IDs */\n");
    fprintf(out, "typedef enum {\n");
    for (int i = 0; i < window_count; i++) {
        char upper[MAX_NAME];
        strncpy(upper, windows[i].name, MAX_NAME - 1);
        to_upper(upper);
        fprintf(out, "    %s_WINDOW_%s = %d,\n", prefix, upper, i);
    }
    fprintf(out, "    %s_WINDOW_COUNT\n", prefix);
    fprintf(out, "} %s_window_t;\n\n", prefix);

    /* UI state struct */
    fprintf(out, "/* UI state (user data bindings) */\n");
    fprintf(out, "typedef struct {\n");
    for (int i = 0; i < widget_count; i++) {
        widget_t *w = &widgets[i];
        if (w->bind[0]) {
            switch (w->type) {
                case WIDGET_SLIDER:
                    fprintf(out, "    float %s;\n", w->name);
                    break;
                case WIDGET_CHECKBOX:
                    fprintf(out, "    bool %s;\n", w->name);
                    break;
                case WIDGET_TEXTBOX:
                    fprintf(out, "    char %s[256];\n", w->name);
                    fprintf(out, "    int %s_len;\n", w->name);
                    break;
                default:
                    break;
            }
        }
    }
    fprintf(out, "    void *user_data;\n");
    fprintf(out, "} %s_ui_state_t;\n\n", prefix);

    /* Function declarations */
    fprintf(out, "/* UI functions */\n");
    fprintf(out, "void %s_ui_init(%s_ui_state_t *state);\n", prefix, prefix);
    fprintf(out, "void %s_ui_render(struct nk_context *ctx, %s_ui_state_t *state);\n\n", prefix, prefix);

    /* Handler prototypes */
    fprintf(out, "/* Event handlers (implement these) */\n");
    for (int i = 0; i < widget_count; i++) {
        widget_t *w = &widgets[i];
        if (w->on_click[0]) {
            fprintf(out, "extern void %s(%s_ui_state_t *state);\n", w->on_click, prefix);
        }
        if (w->on_change[0]) {
            fprintf(out, "extern void %s(%s_ui_state_t *state);\n", w->on_change, prefix);
        }
    }
    fprintf(out, "\n");

    fprintf(out, "#endif /* %s */\n", guard);
    fclose(out);

    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_ui_c(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char header_name[128];
    char impl_name[128];
    char lower_prefix[MAX_NAME];

    strncpy(lower_prefix, prefix, MAX_NAME - 1);
    to_lower(lower_prefix);

    snprintf(header_name, sizeof(header_name), "%s_ui.h", lower_prefix);
    snprintf(impl_name, sizeof(impl_name), "%s_ui.c", lower_prefix);

    snprintf(path, sizeof(path), "%s/%s", outdir, impl_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    fprintf(out, "/* AUTO-GENERATED by uigen %s — DO NOT EDIT */\n\n", UIGEN_VERSION);
    fprintf(out, "#include \"%s\"\n\n", header_name);
    fprintf(out, "/* Include Nuklear implementation */\n");
    fprintf(out, "#define NK_INCLUDE_FIXED_TYPES\n");
    fprintf(out, "#define NK_INCLUDE_STANDARD_IO\n");
    fprintf(out, "#define NK_INCLUDE_DEFAULT_ALLOCATOR\n");
    fprintf(out, "#include \"nuklear.h\"\n\n");

    /* Init function */
    fprintf(out, "void %s_ui_init(%s_ui_state_t *state) {\n", prefix, prefix);
    fprintf(out, "    memset(state, 0, sizeof(*state));\n");
    for (int i = 0; i < widget_count; i++) {
        widget_t *w = &widgets[i];
        if (w->type == WIDGET_SLIDER && w->min_val != w->max_val) {
            fprintf(out, "    state->%s = %d;\n", w->name, w->min_val);
        }
    }
    fprintf(out, "}\n\n");

    /* Render function */
    fprintf(out, "void %s_ui_render(struct nk_context *ctx, %s_ui_state_t *state) {\n", prefix, prefix);

    for (int wi = 0; wi < window_count; wi++) {
        window_t *win = &windows[wi];
        fprintf(out, "    /* Window: %s */\n", win->name);
        fprintf(out, "    if (nk_begin(ctx, \"%s\", nk_rect(50, 50, %d, %d),\n",
                win->title[0] ? win->title : win->name, win->width, win->height);
        fprintf(out, "            NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE)) {\n");

        for (int pi = win->panel_start; pi < win->panel_start + win->panel_count; pi++) {
            panel_t *panel = &panels[pi];
            const char *layout_fn = "nk_layout_row_dynamic";
            if (panel->layout == LAYOUT_HORIZONTAL) {
                layout_fn = "nk_layout_row_static";
            }

            fprintf(out, "\n        /* Panel: %s */\n", panel->name);
            fprintf(out, "        %s(ctx, 30, %d);\n", layout_fn,
                    panel->layout == LAYOUT_HORIZONTAL ? 100 : panel->widget_count);

            for (int wgi = panel->widget_start; wgi < panel->widget_start + panel->widget_count; wgi++) {
                widget_t *w = &widgets[wgi];

                switch (w->type) {
                    case WIDGET_BUTTON:
                        fprintf(out, "        if (nk_button_label(ctx, \"%s\")) {\n",
                                w->label[0] ? w->label : w->name);
                        if (w->on_click[0]) {
                            fprintf(out, "            %s(state);\n", w->on_click);
                        }
                        fprintf(out, "        }\n");
                        break;

                    case WIDGET_LABEL:
                        fprintf(out, "        nk_label(ctx, \"%s\", NK_TEXT_LEFT);\n",
                                w->label[0] ? w->label : w->name);
                        break;

                    case WIDGET_SLIDER:
                        fprintf(out, "        nk_slider_float(ctx, %d, &state->%s, %d, 1);\n",
                                w->min_val, w->name, w->max_val);
                        break;

                    case WIDGET_CHECKBOX:
                        fprintf(out, "        nk_checkbox_label(ctx, \"%s\", (nk_bool*)&state->%s);\n",
                                w->label[0] ? w->label : w->name, w->name);
                        break;

                    case WIDGET_TEXTBOX:
                        fprintf(out, "        nk_edit_string(ctx, NK_EDIT_SIMPLE, state->%s,\n",
                                w->name);
                        fprintf(out, "                       &state->%s_len, 255, nk_filter_default);\n",
                                w->name);
                        break;

                    default:
                        break;
                }
            }
        }

        fprintf(out, "    }\n");
        fprintf(out, "    nk_end(ctx);\n\n");
    }

    fprintf(out, "}\n");

    fclose(out);
    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static void generate_version(const char *outdir, const char *profile) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/GENERATOR_VERSION", outdir);

    FILE *out = fopen(path, "w");
    if (!out) return;

    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    fprintf(out, "uigen %s\n", UIGEN_VERSION);
    fprintf(out, "generated: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
    fprintf(out, "profile: %s\n", profile);
    fprintf(out, "windows: %d\n", window_count);
    fprintf(out, "panels: %d\n", panel_count);
    fprintf(out, "widgets: %d\n", widget_count);

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
    fprintf(stderr, "uigen %s — Nuklear UI Code Generator\n", UIGEN_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: uigen <interface.ui> [output_dir] [prefix]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Spec format:\n");
    fprintf(stderr, "  window Name { title: \"...\", width: N, height: N }\n");
    fprintf(stderr, "  panel Name { layout: vertical|horizontal|grid }\n");
    fprintf(stderr, "  button Name { label: \"...\", on_click: handler }\n");
    fprintf(stderr, "  slider Name { min: N, max: N, bind: var }\n");
    fprintf(stderr, "  label Name { bind: var }\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Output:\n");
    fprintf(stderr, "  <prefix>_ui.h  — UI state and function declarations\n");
    fprintf(stderr, "  <prefix>_ui.c  — Nuklear rendering implementation\n");
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

    if (parse_spec(input) != 0) {
        return 1;
    }

    fprintf(stderr, "Parsed %d windows, %d panels, %d widgets from %s\n",
            window_count, panel_count, widget_count, input);

    if (ensure_output_dir(outdir) != 0) {
        return 1;
    }

    if (generate_ui_h(outdir, prefix) != 0) return 1;
    if (generate_ui_c(outdir, prefix) != 0) return 1;

    generate_version(outdir, profile);

    return 0;
}
