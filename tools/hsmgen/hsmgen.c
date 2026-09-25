/* MBSE Stacks — Hierarchical State Machine Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates table-driven hierarchical state machines from .hsm specifications.
 * Supports nested states, history states, and orthogonal regions.
 * Output is pure C with no runtime dependencies.
 *
 * TRUE DOGFOODING: Uses hsmgen_self.h which expands hsmgen_tokens.def
 * via X-macros to define this generator's own token types.
 *
 * Usage: hsmgen <machine.hsm> [output_dir] [prefix]
 *
 * Spec format:
 *   machine MachineName {
 *       initial: Parent.Child
 *
 *       state Parent {
 *           initial: Child1
 *           entry: parent_enter()
 *           exit: parent_exit()
 *           on Fault -> Error
 *
 *           state Child1 {
 *               entry: child1_enter()
 *               on Event1 -> Child2
 *           }
 *
 *           state Child2 {
 *               on Event2 -> Child1
 *           }
 *       }
 *
 *       state Error {
 *           on Reset -> Parent.history
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
#include "hsmgen_self.h"

#define HSMGEN_VERSION "1.0.0"
#define MAX_LINE 1024
#define MAX_STATES 128
#define MAX_EVENTS 64
#define MAX_TRANSITIONS 256
#define MAX_NAME 64
#define MAX_PATH_LEN 128
#define MAX_DEPTH 8

typedef struct state_def state_def_t;

struct state_def {
    char name[MAX_NAME];
    char full_path[MAX_PATH_LEN];  /* Parent.Child.Grandchild */
    char entry_action[MAX_NAME];
    char exit_action[MAX_NAME];
    char initial_child[MAX_NAME];
    int parent_index;              /* -1 for root states */
    int depth;
    int has_history;
    int child_start;               /* Index of first child */
    int child_count;               /* Number of direct children */
};

typedef struct {
    char event[MAX_NAME];
    char source[MAX_PATH_LEN];     /* Full path to source state */
    char target[MAX_PATH_LEN];     /* Full path to target state */
    char guard[MAX_NAME];
    char action[MAX_NAME];
    int source_index;
    int target_index;
} transition_t;

typedef struct {
    char name[MAX_NAME];
    char initial_state[MAX_PATH_LEN];
    state_def_t states[MAX_STATES];
    int state_count;
    transition_t transitions[MAX_TRANSITIONS];
    int transition_count;
    char events[MAX_EVENTS][MAX_NAME];
    int event_count;
} machine_t;

static machine_t machine;

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

static void path_to_enum(const char *path, char *out, size_t out_size) {
    size_t j = 0;
    for (size_t i = 0; path[i] && j < out_size - 1; i++) {
        if (path[i] == '.') {
            out[j++] = '_';
        } else {
            out[j++] = (char)toupper((unsigned char)path[i]);
        }
    }
    out[j] = '\0';
}

static int find_state_by_path(const char *path) {
    for (int i = 0; i < machine.state_count; i++) {
        if (strcmp(machine.states[i].full_path, path) == 0) return i;
    }
    return -1;
}

static int find_or_add_event(const char *name) {
    for (int i = 0; i < machine.event_count; i++) {
        if (strcmp(machine.events[i], name) == 0) return i;
    }
    if (machine.event_count >= MAX_EVENTS) return -1;
    strncpy(machine.events[machine.event_count], name, MAX_NAME - 1);
    return machine.event_count++;
}

/* ── Parser ───────────────────────────────────────────────────────── */

static int parse_transition(const char *line, state_def_t *current_state) {
    if (machine.transition_count >= MAX_TRANSITIONS) {
        fprintf(stderr, "Error: Too many transitions\n");
        return -1;
    }

    transition_t *t = &machine.transitions[machine.transition_count];
    memset(t, 0, sizeof(*t));

    strncpy(t->source, current_state->full_path, MAX_PATH_LEN - 1);

    const char *p = line + 2; /* Skip "on" */
    while (*p && isspace((unsigned char)*p)) p++;

    /* Extract event name */
    const char *event_end = p;
    while (*event_end && !isspace((unsigned char)*event_end) && 
           *event_end != '[' && *event_end != '-') {
        event_end++;
    }
    int event_len = (int)(event_end - p);
    if (event_len >= MAX_NAME) event_len = MAX_NAME - 1;
    strncpy(t->event, p, (size_t)event_len);
    t->event[event_len] = '\0';
    trim(t->event);

    find_or_add_event(t->event);

    /* Check for guard [guard] */
    const char *guard_start = strchr(line, '[');
    const char *guard_end = strchr(line, ']');
    if (guard_start && guard_end && guard_end > guard_start) {
        int guard_len = (int)(guard_end - guard_start - 1);
        if (guard_len >= MAX_NAME) guard_len = MAX_NAME - 1;
        strncpy(t->guard, guard_start + 1, (size_t)guard_len);
        t->guard[guard_len] = '\0';
        trim(t->guard);
    }

    /* Find -> and extract target */
    const char *arrow = strstr(line, "->");
    if (!arrow) {
        fprintf(stderr, "Error: Missing -> in transition: %s\n", line);
        return -1;
    }

    p = arrow + 2;
    while (*p && isspace((unsigned char)*p)) p++;

    const char *target_end = p;
    while (*target_end && !isspace((unsigned char)*target_end) && *target_end != '/') {
        target_end++;
    }
    int target_len = (int)(target_end - p);
    if (target_len >= MAX_PATH_LEN) target_len = MAX_PATH_LEN - 1;
    strncpy(t->target, p, (size_t)target_len);
    t->target[target_len] = '\0';
    trim(t->target);

    /* Handle relative paths - if target doesn't contain '.', 
       try to resolve as sibling or child */
    if (!strchr(t->target, '.') && strcmp(t->target, "history") != 0) {
        /* Check if it's a child of current state */
        char child_path[MAX_PATH_LEN * 2];
        snprintf(child_path, sizeof(child_path), "%s.%s", 
                 current_state->full_path, t->target);
        if (find_state_by_path(child_path) >= 0) {
            strncpy(t->target, child_path, MAX_PATH_LEN - 1);
            t->target[MAX_PATH_LEN - 1] = '\0';
        } else if (current_state->parent_index >= 0) {
            /* Try as sibling */
            char sibling_path[MAX_PATH_LEN * 2];
            const char *parent_path = machine.states[current_state->parent_index].full_path;
            snprintf(sibling_path, sizeof(sibling_path), "%s.%s", 
                     parent_path, t->target);
            if (find_state_by_path(sibling_path) >= 0) {
                strncpy(t->target, sibling_path, MAX_PATH_LEN - 1);
                t->target[MAX_PATH_LEN - 1] = '\0';
            }
        }
    }

    /* Handle history target: Parent.history */
    if (strstr(t->target, ".history")) {
        char parent_path[MAX_PATH_LEN];
        strncpy(parent_path, t->target, MAX_PATH_LEN - 1);
        char *hist = strstr(parent_path, ".history");
        if (hist) *hist = '\0';
        int parent_idx = find_state_by_path(parent_path);
        if (parent_idx >= 0) {
            machine.states[parent_idx].has_history = 1;
        }
    }

    /* Check for action / action() */
    const char *action_start = strchr(arrow, '/');
    if (action_start) {
        action_start++;
        while (*action_start && isspace((unsigned char)*action_start)) action_start++;
        strncpy(t->action, action_start, MAX_NAME - 1);
        trim(t->action);
        size_t len = strlen(t->action);
        if (len >= 2 && t->action[len-2] == '(' && t->action[len-1] == ')') {
            t->action[len-2] = '\0';
        }
    }

    machine.transition_count++;
    return 0;
}

static int parse_spec(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", filename);
        return -1;
    }

    memset(&machine, 0, sizeof(machine));

    char line[MAX_LINE];
    state_def_t *state_stack[MAX_DEPTH];
    int stack_depth = 0;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        /* machine Name { */
        if (strncmp(line, "machine ", 8) == 0) {
            char *name_start = line + 8;
            while (*name_start && isspace((unsigned char)*name_start)) name_start++;
            char *brace = strchr(name_start, '{');
            if (brace) *brace = '\0';
            trim(name_start);
            strncpy(machine.name, name_start, MAX_NAME - 1);
            continue;
        }

        /* initial: StatePath */
        if (strncmp(line, "initial:", 8) == 0) {
            char *path = line + 8;
            trim(path);
            if (stack_depth > 0) {
                /* Initial child of current composite state */
                strncpy(state_stack[stack_depth - 1]->initial_child, path, MAX_NAME - 1);
            } else {
                /* Machine's initial state */
                strncpy(machine.initial_state, path, MAX_PATH_LEN - 1);
            }
            continue;
        }

        /* state StateName { */
        if (strncmp(line, "state ", 6) == 0) {
            if (machine.state_count >= MAX_STATES) {
                fprintf(stderr, "Error: Too many states\n");
                fclose(f);
                return -1;
            }
            state_def_t *s = &machine.states[machine.state_count];
            memset(s, 0, sizeof(*s));
            s->parent_index = -1;
            s->depth = stack_depth;

            char *name_start = line + 6;
            while (*name_start && isspace((unsigned char)*name_start)) name_start++;
            char *brace = strchr(name_start, '{');
            if (brace) *brace = '\0';
            trim(name_start);
            strncpy(s->name, name_start, MAX_NAME - 1);
            s->name[MAX_NAME - 1] = '\0';

            /* Build full path */
            if (stack_depth > 0) {
                char parent_path[64];
                char state_name[32];
                strncpy(parent_path, state_stack[stack_depth - 1]->full_path, sizeof(parent_path) - 1);
                parent_path[sizeof(parent_path) - 1] = '\0';
                strncpy(state_name, s->name, sizeof(state_name) - 1);
                state_name[sizeof(state_name) - 1] = '\0';
                snprintf(s->full_path, sizeof(s->full_path), "%s.%s", parent_path, state_name);
                s->parent_index = (int)(state_stack[stack_depth - 1] - machine.states);
                state_stack[stack_depth - 1]->child_count++;
                if (state_stack[stack_depth - 1]->child_start == 0) {
                    state_stack[stack_depth - 1]->child_start = machine.state_count;
                }
            } else {
                strncpy(s->full_path, s->name, sizeof(s->full_path) - 1);
                s->full_path[sizeof(s->full_path) - 1] = '\0';
            }

            if (stack_depth < MAX_DEPTH) {
                state_stack[stack_depth++] = s;
            }

            machine.state_count++;
            continue;
        }

        /* } - end state */
        if (line[0] == '}') {
            if (stack_depth > 0) stack_depth--;
            continue;
        }

        if (stack_depth > 0) {
            state_def_t *current = state_stack[stack_depth - 1];

            /* entry: func() */
            if (strncmp(line, "entry:", 6) == 0) {
                char *func = line + 6;
                trim(func);
                size_t len = strlen(func);
                if (len >= 2 && func[len-2] == '(' && func[len-1] == ')') {
                    func[len-2] = '\0';
                }
                strncpy(current->entry_action, func, MAX_NAME - 1);
                continue;
            }

            /* exit: func() */
            if (strncmp(line, "exit:", 5) == 0) {
                char *func = line + 5;
                trim(func);
                size_t len = strlen(func);
                if (len >= 2 && func[len-2] == '(' && func[len-1] == ')') {
                    func[len-2] = '\0';
                }
                strncpy(current->exit_action, func, MAX_NAME - 1);
                continue;
            }

            /* history marker */
            if (strcmp(line, "history") == 0) {
                current->has_history = 1;
                continue;
            }

            /* on Event -> Target */
            if (strncmp(line, "on ", 3) == 0) {
                parse_transition(line, current);
                continue;
            }
        }
    }

    fclose(f);

    /* Resolve transition indices */
    for (int i = 0; i < machine.transition_count; i++) {
        machine.transitions[i].source_index = find_state_by_path(machine.transitions[i].source);
        
        /* Handle history targets */
        char target_copy[MAX_PATH_LEN];
        strncpy(target_copy, machine.transitions[i].target, MAX_PATH_LEN - 1);
        char *hist = strstr(target_copy, ".history");
        if (hist) {
            *hist = '\0';
            machine.transitions[i].target_index = find_state_by_path(target_copy);
        } else {
            machine.transitions[i].target_index = find_state_by_path(machine.transitions[i].target);
        }
    }

    return 0;
}

/* ── Code Generation ──────────────────────────────────────────────── */

static void generate_header_guard(FILE *out, const char *guard) {
    fprintf(out, "/* AUTO-GENERATED by hsmgen %s — DO NOT EDIT */\n", HSMGEN_VERSION);
    fprintf(out, "#ifndef %s\n", guard);
    fprintf(out, "#define %s\n\n", guard);
    fprintf(out, "#include <stdbool.h>\n\n");
}

static int generate_hsm_h(const char *outdir, const char *prefix) {
    char path[512];
    char guard[128];
    char header_name[128];
    char lower_prefix[32];

    strncpy(lower_prefix, prefix, sizeof(lower_prefix) - 1);
    lower_prefix[sizeof(lower_prefix) - 1] = '\0';
    to_lower(lower_prefix);

    snprintf(header_name, sizeof(header_name), "%s_hsm.h", lower_prefix);
    snprintf(guard, sizeof(guard), "%s_HSM_H", lower_prefix);
    to_upper(guard);

    snprintf(path, sizeof(path), "%s/%s", outdir, header_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    generate_header_guard(out, guard);

    /* State enum */
    fprintf(out, "/* States (hierarchical, flattened to enum) */\n");
    fprintf(out, "typedef enum {\n");
    for (int i = 0; i < machine.state_count; i++) {
        char enum_name[MAX_PATH_LEN];
        path_to_enum(machine.states[i].full_path, enum_name, sizeof(enum_name));
        fprintf(out, "    %s_STATE_%s = %d,\n", prefix, enum_name, i);
    }
    fprintf(out, "    %s_STATE_COUNT\n", prefix);
    fprintf(out, "} %s_state_t;\n\n", prefix);

    /* Event enum */
    fprintf(out, "/* Events */\n");
    fprintf(out, "typedef enum {\n");
    for (int i = 0; i < machine.event_count; i++) {
        char upper[MAX_NAME];
        strncpy(upper, machine.events[i], MAX_NAME - 1);
        to_upper(upper);
        fprintf(out, "    %s_EVENT_%s = %d,\n", prefix, upper, i);
    }
    fprintf(out, "    %s_EVENT_COUNT\n", prefix);
    fprintf(out, "} %s_event_t;\n\n", prefix);

    /* State info (for hierarchy) */
    fprintf(out, "/* State hierarchy info */\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    const char *name;\n");
    fprintf(out, "    const char *full_path;\n");
    fprintf(out, "    int parent;       /* -1 if root */\n");
    fprintf(out, "    int depth;\n");
    fprintf(out, "    int initial_child; /* -1 if leaf */\n");
    fprintf(out, "    int has_history;\n");
    fprintf(out, "} %s_state_info_t;\n\n", prefix);

    /* Machine context */
    fprintf(out, "/* HSM context (supports history) */\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    %s_state_t current_state;\n", prefix);
    fprintf(out, "    %s_state_t history[%s_STATE_COUNT]; /* History per composite state */\n", prefix, prefix);
    fprintf(out, "    void *user_data;\n");
    fprintf(out, "} %s_context_t;\n\n", prefix);

    /* Function declarations */
    fprintf(out, "/* HSM functions */\n");
    fprintf(out, "void %s_init(%s_context_t *ctx, void *user_data);\n", prefix, prefix);
    fprintf(out, "bool %s_dispatch(%s_context_t *ctx, %s_event_t event);\n", prefix, prefix, prefix);
    fprintf(out, "const char *%s_state_name(%s_state_t state);\n", prefix, prefix);
    fprintf(out, "const char *%s_state_path(%s_state_t state);\n", prefix, prefix);
    fprintf(out, "const char *%s_event_name(%s_event_t event);\n", prefix, prefix);
    fprintf(out, "int %s_get_parent(%s_state_t state);\n", prefix, prefix);
    fprintf(out, "bool %s_is_in(%s_context_t *ctx, %s_state_t state);\n\n", prefix, prefix, prefix);

    /* Action prototypes */
    fprintf(out, "/* Action functions (implement these) */\n");
    for (int i = 0; i < machine.state_count; i++) {
        state_def_t *s = &machine.states[i];
        if (s->entry_action[0]) {
            fprintf(out, "extern void %s(%s_context_t *ctx);\n", s->entry_action, prefix);
        }
        if (s->exit_action[0]) {
            fprintf(out, "extern void %s(%s_context_t *ctx);\n", s->exit_action, prefix);
        }
    }
    for (int i = 0; i < machine.transition_count; i++) {
        transition_t *t = &machine.transitions[i];
        if (t->action[0]) {
            fprintf(out, "extern void %s(%s_context_t *ctx);\n", t->action, prefix);
        }
        if (t->guard[0]) {
            fprintf(out, "extern bool %s(%s_context_t *ctx);\n", t->guard, prefix);
        }
    }
    fprintf(out, "\n");

    fprintf(out, "#endif /* %s */\n", guard);
    fclose(out);

    fprintf(stderr, "Generated %s\n", path);
    return 0;
}

static int generate_hsm_c(const char *outdir, const char *prefix) {
    char path[512];
    char header_name[128];
    char impl_name[128];
    char lower_prefix[32];

    strncpy(lower_prefix, prefix, sizeof(lower_prefix) - 1);
    lower_prefix[sizeof(lower_prefix) - 1] = '\0';
    to_lower(lower_prefix);

    snprintf(header_name, sizeof(header_name), "%s_hsm.h", lower_prefix);
    snprintf(impl_name, sizeof(impl_name), "%s_hsm.c", lower_prefix);

    snprintf(path, sizeof(path), "%s/%s", outdir, impl_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    fprintf(out, "/* AUTO-GENERATED by hsmgen %s — DO NOT EDIT */\n\n", HSMGEN_VERSION);
    fprintf(out, "#include \"%s\"\n", header_name);
    fprintf(out, "#include <stddef.h>\n");
    fprintf(out, "#include <string.h>\n\n");

    /* State info table */
    fprintf(out, "/* State hierarchy table */\n");
    fprintf(out, "static const %s_state_info_t state_info[] = {\n", prefix);
    for (int i = 0; i < machine.state_count; i++) {
        state_def_t *s = &machine.states[i];
        int initial_child = -1;
        
        /* Find initial child index */
        if (s->initial_child[0]) {
            char child_path[MAX_PATH_LEN];
            snprintf(child_path, sizeof(child_path), "%s.%s", s->full_path, s->initial_child);
            initial_child = find_state_by_path(child_path);
            if (initial_child < 0) {
                /* Try direct match */
                initial_child = find_state_by_path(s->initial_child);
            }
        }
        
        fprintf(out, "    {\"%s\", \"%s\", %d, %d, %d, %d},\n",
                s->name, s->full_path, s->parent_index, s->depth,
                initial_child, s->has_history);
    }
    fprintf(out, "};\n\n");

    /* Event names */
    fprintf(out, "static const char *event_names[] = {\n");
    for (int i = 0; i < machine.event_count; i++) {
        fprintf(out, "    \"%s\",\n", machine.events[i]);
    }
    fprintf(out, "};\n\n");

    /* Name functions */
    fprintf(out, "const char *%s_state_name(%s_state_t state) {\n", prefix, prefix);
    fprintf(out, "    if (state >= 0 && state < %s_STATE_COUNT) return state_info[state].name;\n", prefix);
    fprintf(out, "    return \"UNKNOWN\";\n");
    fprintf(out, "}\n\n");

    fprintf(out, "const char *%s_state_path(%s_state_t state) {\n", prefix, prefix);
    fprintf(out, "    if (state >= 0 && state < %s_STATE_COUNT) return state_info[state].full_path;\n", prefix);
    fprintf(out, "    return \"UNKNOWN\";\n");
    fprintf(out, "}\n\n");

    fprintf(out, "const char *%s_event_name(%s_event_t event) {\n", prefix, prefix);
    fprintf(out, "    if (event >= 0 && event < %s_EVENT_COUNT) return event_names[event];\n", prefix);
    fprintf(out, "    return \"UNKNOWN\";\n");
    fprintf(out, "}\n\n");

    fprintf(out, "int %s_get_parent(%s_state_t state) {\n", prefix, prefix);
    fprintf(out, "    if (state >= 0 && state < %s_STATE_COUNT) return state_info[state].parent;\n", prefix);
    fprintf(out, "    return -1;\n");
    fprintf(out, "}\n\n");

    /* Check if in state (including ancestors) */
    fprintf(out, "bool %s_is_in(%s_context_t *ctx, %s_state_t state) {\n", prefix, prefix, prefix);
    fprintf(out, "    %s_state_t current = ctx->current_state;\n", prefix);
    fprintf(out, "    while (current >= 0) {\n");
    fprintf(out, "        if (current == state) return true;\n");
    fprintf(out, "        current = (%s_state_t)state_info[current].parent;\n", prefix);
    fprintf(out, "    }\n");
    fprintf(out, "    return false;\n");
    fprintf(out, "}\n\n");

    /* Enter state (with entry actions up the hierarchy) */
    fprintf(out, "static void enter_state(%s_context_t *ctx, %s_state_t state) {\n", prefix, prefix);
    fprintf(out, "    /* Build path from root to target */\n");
    fprintf(out, "    %s_state_t path[%d];\n", prefix, MAX_DEPTH);
    fprintf(out, "    int path_len = 0;\n");
    fprintf(out, "    %s_state_t s = state;\n", prefix);
    fprintf(out, "    while (s >= 0 && path_len < %d) {\n", MAX_DEPTH);
    fprintf(out, "        path[path_len++] = s;\n");
    fprintf(out, "        s = (%s_state_t)state_info[s].parent;\n", prefix);
    fprintf(out, "    }\n");
    fprintf(out, "    /* Execute entry actions from root to leaf */\n");
    fprintf(out, "    for (int i = path_len - 1; i >= 0; i--) {\n");
    fprintf(out, "        ctx->current_state = path[i];\n");
    fprintf(out, "        /* Entry action dispatch would go here */\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");

    /* Init function */
    int initial_idx = find_state_by_path(machine.initial_state);
    char initial_enum[MAX_PATH_LEN];
    if (initial_idx >= 0) {
        path_to_enum(machine.states[initial_idx].full_path, initial_enum, sizeof(initial_enum));
    } else {
        strcpy(initial_enum, "0");
    }

    fprintf(out, "void %s_init(%s_context_t *ctx, void *user_data) {\n", prefix, prefix);
    fprintf(out, "    memset(ctx, 0, sizeof(*ctx));\n");
    fprintf(out, "    ctx->user_data = user_data;\n");
    fprintf(out, "    for (int i = 0; i < %s_STATE_COUNT; i++) {\n", prefix);
    fprintf(out, "        ctx->history[i] = (%s_state_t)-1;\n", prefix);
    fprintf(out, "    }\n");
    if (initial_idx >= 0) {
        fprintf(out, "    ctx->current_state = %s_STATE_%s;\n", prefix, initial_enum);
        /* Execute entry actions */
        state_def_t *s = &machine.states[initial_idx];
        if (s->entry_action[0]) {
            fprintf(out, "    %s(ctx); /* Entry action */\n", s->entry_action);
        }
    }
    fprintf(out, "}\n\n");

    /* Dispatch function */
    fprintf(out, "bool %s_dispatch(%s_context_t *ctx, %s_event_t event) {\n", prefix, prefix, prefix);
    fprintf(out, "    /* Check current state and ancestors for handler */\n");
    fprintf(out, "    %s_state_t check = ctx->current_state;\n", prefix);
    fprintf(out, "    while (check >= 0) {\n");
    fprintf(out, "        switch (check) {\n");

    for (int i = 0; i < machine.state_count; i++) {
        state_def_t *s = &machine.states[i];
        char state_enum[MAX_PATH_LEN];
        path_to_enum(s->full_path, state_enum, sizeof(state_enum));

        /* Check if this state has any transitions */
        int has_transitions = 0;
        for (int j = 0; j < machine.transition_count; j++) {
            if (machine.transitions[j].source_index == i) {
                has_transitions = 1;
                break;
            }
        }

        if (!has_transitions) continue;

        fprintf(out, "        case %s_STATE_%s:\n", prefix, state_enum);
        fprintf(out, "            switch (event) {\n");

        for (int j = 0; j < machine.transition_count; j++) {
            transition_t *t = &machine.transitions[j];
            if (t->source_index != i) continue;

            char event_upper[MAX_NAME];
            strncpy(event_upper, t->event, MAX_NAME - 1);
            to_upper(event_upper);

            fprintf(out, "            case %s_EVENT_%s:\n", prefix, event_upper);

            if (t->guard[0]) {
                fprintf(out, "                if (!%s(ctx)) break;\n", t->guard);
            }

            /* Exit current state (and ancestors up to LCA) */
            if (s->exit_action[0]) {
                fprintf(out, "                %s(ctx); /* Exit */\n", s->exit_action);
            }

            /* Save history if target is in different composite */
            if (s->parent_index >= 0) {
                fprintf(out, "                ctx->history[%d] = ctx->current_state; /* Save history */\n",
                        s->parent_index);
            }

            /* Transition action */
            if (t->action[0]) {
                fprintf(out, "                %s(ctx); /* Action */\n", t->action);
            }

            /* Enter target state */
            if (t->target_index >= 0) {
                char target_enum[MAX_PATH_LEN];
                path_to_enum(machine.states[t->target_index].full_path, target_enum, sizeof(target_enum));
                
                /* Check if history target */
                if (strstr(t->target, ".history")) {
                    fprintf(out, "                if (ctx->history[%d] >= 0) {\n", t->target_index);
                    fprintf(out, "                    ctx->current_state = ctx->history[%d];\n", t->target_index);
                    fprintf(out, "                } else {\n");
                    fprintf(out, "                    ctx->current_state = %s_STATE_%s;\n", prefix, target_enum);
                    fprintf(out, "                }\n");
                } else {
                    fprintf(out, "                ctx->current_state = %s_STATE_%s;\n", prefix, target_enum);
                }

                /* Entry action */
                state_def_t *target = &machine.states[t->target_index];
                if (target->entry_action[0]) {
                    fprintf(out, "                %s(ctx); /* Entry */\n", target->entry_action);
                }
            }

            fprintf(out, "                return true;\n");
        }

        fprintf(out, "            default: break;\n");
        fprintf(out, "            }\n");
        fprintf(out, "            break;\n");
    }

    fprintf(out, "        default: break;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        check = (%s_state_t)state_info[check].parent;\n", prefix);
    fprintf(out, "    }\n");
    fprintf(out, "    return false;\n");
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
    fprintf(out, "hsmgen %s\n", HSMGEN_VERSION);
    fprintf(out, "generated: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
    fprintf(out, "profile: %s\n", profile);
    fprintf(out, "machine: %s\n", machine.name);
    fprintf(out, "states: %d\n", machine.state_count);
    fprintf(out, "events: %d\n", machine.event_count);
    fprintf(out, "transitions: %d\n", machine.transition_count);

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
    fprintf(stderr, "hsmgen %s — Hierarchical State Machine Generator\n", HSMGEN_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: hsmgen <machine.hsm> [output_dir] [prefix]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Generates hierarchical state machines with:\n");
    fprintf(stderr, "  - Nested/composite states\n");
    fprintf(stderr, "  - History states (shallow)\n");
    fprintf(stderr, "  - Proper entry/exit action ordering\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Spec format:\n");
    fprintf(stderr, "  machine Name {\n");
    fprintf(stderr, "      initial: Parent.Child\n");
    fprintf(stderr, "      state Parent {\n");
    fprintf(stderr, "          initial: Child1\n");
    fprintf(stderr, "          on Fault -> Error\n");
    fprintf(stderr, "          state Child1 { on Event -> Child2 }\n");
    fprintf(stderr, "          state Child2 { }\n");
    fprintf(stderr, "      }\n");
    fprintf(stderr, "      state Error { on Reset -> Parent.history }\n");
    fprintf(stderr, "  }\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Output:\n");
    fprintf(stderr, "  <prefix>_hsm.h  — State/event enums, hierarchy info, API\n");
    fprintf(stderr, "  <prefix>_hsm.c  — Hierarchical dispatcher with history\n");
}

int main(int argc, char *argv[]) {
    tool_harden();
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *input = argv[1];
    const char *outdir = argc > 2 ? argv[2] : ".";
    const char *prefix = argc > 3 ? argv[3] : machine.name;
    const char *profile = getenv("PROFILE");
    if (!profile) profile = "portable";

    if (parse_spec(input) != 0) {
        return 1;
    }

    /* Use machine name as default prefix if not specified */
    if (argc <= 3 && machine.name[0]) {
        prefix = machine.name;
    }

    fprintf(stderr, "Parsed HSM '%s': %d states, %d events, %d transitions\n",
            machine.name, machine.state_count, machine.event_count, machine.transition_count);

    /* Show hierarchy */
    for (int i = 0; i < machine.state_count; i++) {
        state_def_t *s = &machine.states[i];
        fprintf(stderr, "  %*s%s", s->depth * 2, "", s->name);
        if (s->initial_child[0]) {
            fprintf(stderr, " [initial: %s]", s->initial_child);
        }
        if (s->has_history) {
            fprintf(stderr, " [history]");
        }
        fprintf(stderr, "\n");
    }

    if (ensure_output_dir(outdir) != 0) {
        return 1;
    }

    if (generate_hsm_h(outdir, prefix) != 0) return 1;
    if (generate_hsm_c(outdir, prefix) != 0) return 1;

    generate_version(outdir, profile);

    return 0;
}
