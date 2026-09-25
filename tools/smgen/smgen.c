/* MBSE Stacks — State Machine Generator
 * Ring 0: Pure C, minimal bootstrap
 *
 * Generates table-driven state machines from .sm specifications.
 * Output is pure C with no runtime dependencies.
 *
 * TRUE DOGFOODING: Uses smgen_self.h which expands smgen_tokens.def
 * via X-macros to define this generator's own token types.
 *
 * Usage: smgen <machine.sm> [output_dir] [prefix]
 *
 * Spec format:
 *   machine MachineName {
 *       initial: StateName
 *
 *       state StateName {
 *           entry: entry_func()
 *           exit: exit_func()
 *           on EventName -> TargetState
 *           on EventName [guard] -> TargetState / action()
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
#include "smgen_self.h"

#define SMGEN_VERSION "1.0.0"
#define MAX_LINE 1024
#define MAX_STATES 64
#define MAX_EVENTS 64
#define MAX_TRANSITIONS 256
#define MAX_NAME 64
#define MAX_PATH 512

typedef struct {
    char name[MAX_NAME];
    char entry_action[MAX_NAME];
    char exit_action[MAX_NAME];
} state_def_t;

typedef struct {
    char event[MAX_NAME];
    char source[MAX_NAME];
    char target[MAX_NAME];
    char guard[MAX_NAME];
    char action[MAX_NAME];
} transition_t;

typedef struct {
    char name[MAX_NAME];
    char initial_state[MAX_NAME];
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

static int find_state(const char *name) {
    for (int i = 0; i < machine.state_count; i++) {
        if (strcmp(machine.states[i].name, name) == 0) return i;
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

static int parse_transition(const char *line, const char *current_state) {
    /* Format: on EventName -> TargetState
     *         on EventName [guard] -> TargetState / action()
     */
    if (machine.transition_count >= MAX_TRANSITIONS) {
        fprintf(stderr, "Error: Too many transitions\n");
        return -1;
    }

    transition_t *t = &machine.transitions[machine.transition_count];
    memset(t, 0, sizeof(*t));

    strncpy(t->source, current_state, MAX_NAME - 1);

    const char *p = line + 2; /* Skip "on" */
    while (*p && isspace((unsigned char)*p)) p++;

    /* Extract event name */
    const char *event_end = p;
    while (*event_end && !isspace((unsigned char)*event_end) && *event_end != '[' && *event_end != '-') {
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
    if (target_len >= MAX_NAME) target_len = MAX_NAME - 1;
    strncpy(t->target, p, (size_t)target_len);
    t->target[target_len] = '\0';
    trim(t->target);

    /* Check for action / action() */
    const char *action_start = strchr(arrow, '/');
    if (action_start) {
        action_start++;
        while (*action_start && isspace((unsigned char)*action_start)) action_start++;
        strncpy(t->action, action_start, MAX_NAME - 1);
        trim(t->action);
        /* Remove trailing () if present */
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
    state_def_t *current_state = NULL;

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

        /* initial: StateName */
        if (strncmp(line, "initial:", 8) == 0) {
            char *name = line + 8;
            trim(name);
            strncpy(machine.initial_state, name, MAX_NAME - 1);
            continue;
        }

        /* state StateName { */
        if (strncmp(line, "state ", 6) == 0) {
            if (machine.state_count >= MAX_STATES) {
                fprintf(stderr, "Error: Too many states\n");
                fclose(f);
                return -1;
            }
            current_state = &machine.states[machine.state_count++];
            memset(current_state, 0, sizeof(*current_state));

            char *name_start = line + 6;
            while (*name_start && isspace((unsigned char)*name_start)) name_start++;
            char *brace = strchr(name_start, '{');
            if (brace) *brace = '\0';
            trim(name_start);
            strncpy(current_state->name, name_start, MAX_NAME - 1);
            continue;
        }

        /* } - end state or machine */
        if (line[0] == '}') {
            current_state = NULL;
            continue;
        }

        if (current_state) {
            /* entry: func() */
            if (strncmp(line, "entry:", 6) == 0) {
                char *func = line + 6;
                trim(func);
                /* Remove trailing () */
                size_t len = strlen(func);
                if (len >= 2 && func[len-2] == '(' && func[len-1] == ')') {
                    func[len-2] = '\0';
                }
                strncpy(current_state->entry_action, func, MAX_NAME - 1);
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
                strncpy(current_state->exit_action, func, MAX_NAME - 1);
                continue;
            }

            /* on Event -> Target */
            if (strncmp(line, "on ", 3) == 0) {
                parse_transition(line, current_state->name);
                continue;
            }
        }
    }

    fclose(f);
    return 0;
}

/* ── Code Generation ──────────────────────────────────────────────── */

static void generate_header_guard(FILE *out, const char *guard) {
    fprintf(out, "/* AUTO-GENERATED by smgen %s — DO NOT EDIT */\n", SMGEN_VERSION);
    fprintf(out, "#ifndef %s\n", guard);
    fprintf(out, "#define %s\n\n", guard);
    fprintf(out, "#include <stdbool.h>\n\n");
}

static int generate_sm_h(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char guard[128];
    char header_name[128];
    char lower_prefix[MAX_NAME];

    strncpy(lower_prefix, prefix, MAX_NAME - 1);
    to_lower(lower_prefix);

    snprintf(header_name, sizeof(header_name), "%s_sm.h", lower_prefix);
    snprintf(guard, sizeof(guard), "%s_SM_H", prefix);
    to_upper(guard);

    snprintf(path, sizeof(path), "%s/%s", outdir, header_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    generate_header_guard(out, guard);

    /* State enum */
    fprintf(out, "/* States */\n");
    fprintf(out, "typedef enum {\n");
    for (int i = 0; i < machine.state_count; i++) {
        char upper_name[MAX_NAME];
        strncpy(upper_name, machine.states[i].name, MAX_NAME - 1);
        to_upper(upper_name);
        fprintf(out, "    %s_STATE_%s = %d,\n", prefix, upper_name, i);
    }
    fprintf(out, "    %s_STATE_COUNT\n", prefix);
    fprintf(out, "} %s_state_t;\n\n", prefix);

    /* Event enum */
    fprintf(out, "/* Events */\n");
    fprintf(out, "typedef enum {\n");
    for (int i = 0; i < machine.event_count; i++) {
        char upper_name[MAX_NAME];
        strncpy(upper_name, machine.events[i], MAX_NAME - 1);
        to_upper(upper_name);
        fprintf(out, "    %s_EVENT_%s = %d,\n", prefix, upper_name, i);
    }
    fprintf(out, "    %s_EVENT_COUNT\n", prefix);
    fprintf(out, "} %s_event_t;\n\n", prefix);

    /* Machine context */
    fprintf(out, "/* Machine context */\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    %s_state_t current_state;\n", prefix);
    fprintf(out, "    void *user_data;\n");
    fprintf(out, "} %s_context_t;\n\n", prefix);

    /* Function declarations */
    fprintf(out, "/* Machine functions */\n");
    fprintf(out, "void %s_init(%s_context_t *ctx, void *user_data);\n", prefix, prefix);
    fprintf(out, "bool %s_dispatch(%s_context_t *ctx, %s_event_t event);\n", prefix, prefix, prefix);
    fprintf(out, "const char *%s_state_name(%s_state_t state);\n", prefix, prefix);
    fprintf(out, "const char *%s_event_name(%s_event_t event);\n\n", prefix, prefix);

    /* Action function prototypes (user must implement) */
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

static int generate_sm_c(const char *outdir, const char *prefix) {
    char path[MAX_PATH];
    char header_name[128];
    char impl_name[128];
    char lower_prefix[MAX_NAME];

    strncpy(lower_prefix, prefix, MAX_NAME - 1);
    to_lower(lower_prefix);

    snprintf(header_name, sizeof(header_name), "%s_sm.h", lower_prefix);
    snprintf(impl_name, sizeof(impl_name), "%s_sm.c", lower_prefix);

    snprintf(path, sizeof(path), "%s/%s", outdir, impl_name);
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return -1;
    }

    fprintf(out, "/* AUTO-GENERATED by smgen %s — DO NOT EDIT */\n\n", SMGEN_VERSION);
    fprintf(out, "#include \"%s\"\n", header_name);
    fprintf(out, "#include <stddef.h>\n\n");

    /* State names */
    fprintf(out, "static const char *state_names[] = {\n");
    for (int i = 0; i < machine.state_count; i++) {
        fprintf(out, "    \"%s\",\n", machine.states[i].name);
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
    fprintf(out, "    if (state >= 0 && state < %s_STATE_COUNT) return state_names[state];\n", prefix);
    fprintf(out, "    return \"UNKNOWN\";\n");
    fprintf(out, "}\n\n");

    fprintf(out, "const char *%s_event_name(%s_event_t event) {\n", prefix, prefix);
    fprintf(out, "    if (event >= 0 && event < %s_EVENT_COUNT) return event_names[event];\n", prefix);
    fprintf(out, "    return \"UNKNOWN\";\n");
    fprintf(out, "}\n\n");

    /* Init function */
    int initial_idx = find_state(machine.initial_state);
    char upper_initial[MAX_NAME];
    strncpy(upper_initial, machine.initial_state, MAX_NAME - 1);
    to_upper(upper_initial);

    fprintf(out, "void %s_init(%s_context_t *ctx, void *user_data) {\n", prefix, prefix);
    fprintf(out, "    ctx->current_state = %s_STATE_%s;\n", prefix, upper_initial);
    fprintf(out, "    ctx->user_data = user_data;\n");
    if (initial_idx >= 0 && machine.states[initial_idx].entry_action[0]) {
        fprintf(out, "    %s(ctx); /* Entry action */\n", machine.states[initial_idx].entry_action);
    }
    fprintf(out, "}\n\n");

    /* Dispatch function */
    fprintf(out, "bool %s_dispatch(%s_context_t *ctx, %s_event_t event) {\n", prefix, prefix, prefix);
    fprintf(out, "    switch (ctx->current_state) {\n");

    for (int i = 0; i < machine.state_count; i++) {
        state_def_t *s = &machine.states[i];
        char upper_state[MAX_NAME];
        strncpy(upper_state, s->name, MAX_NAME - 1);
        to_upper(upper_state);

        fprintf(out, "    case %s_STATE_%s:\n", prefix, upper_state);
        fprintf(out, "        switch (event) {\n");

        for (int j = 0; j < machine.transition_count; j++) {
            transition_t *t = &machine.transitions[j];
            if (strcmp(t->source, s->name) != 0) continue;

            char upper_event[MAX_NAME];
            strncpy(upper_event, t->event, MAX_NAME - 1);
            to_upper(upper_event);

            char upper_target[MAX_NAME];
            strncpy(upper_target, t->target, MAX_NAME - 1);
            to_upper(upper_target);

            fprintf(out, "        case %s_EVENT_%s:\n", prefix, upper_event);

            if (t->guard[0]) {
                fprintf(out, "            if (!%s(ctx)) return false;\n", t->guard);
            }

            if (s->exit_action[0]) {
                fprintf(out, "            %s(ctx); /* Exit */\n", s->exit_action);
            }

            if (t->action[0]) {
                fprintf(out, "            %s(ctx); /* Transition action */\n", t->action);
            }

            fprintf(out, "            ctx->current_state = %s_STATE_%s;\n", prefix, upper_target);

            int target_idx = find_state(t->target);
            if (target_idx >= 0 && machine.states[target_idx].entry_action[0]) {
                fprintf(out, "            %s(ctx); /* Entry */\n", machine.states[target_idx].entry_action);
            }

            fprintf(out, "            return true;\n");
        }

        fprintf(out, "        default: break;\n");
        fprintf(out, "        }\n");
        fprintf(out, "        break;\n");
    }

    fprintf(out, "    default: break;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return false;\n");
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
    fprintf(out, "smgen %s\n", SMGEN_VERSION);
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
    fprintf(stderr, "smgen %s — Table-Driven State Machine Generator\n", SMGEN_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: smgen <machine.sm> [output_dir] [prefix]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Spec format:\n");
    fprintf(stderr, "  machine Name {\n");
    fprintf(stderr, "      initial: StateName\n");
    fprintf(stderr, "      state StateName {\n");
    fprintf(stderr, "          entry: entry_func()\n");
    fprintf(stderr, "          exit: exit_func()\n");
    fprintf(stderr, "          on Event -> Target\n");
    fprintf(stderr, "          on Event [guard] -> Target / action()\n");
    fprintf(stderr, "      }\n");
    fprintf(stderr, "  }\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Output:\n");
    fprintf(stderr, "  <prefix>_sm.h  — State/event enums and API\n");
    fprintf(stderr, "  <prefix>_sm.c  — Table-driven dispatcher\n");
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

    fprintf(stderr, "Parsed machine '%s': %d states, %d events, %d transitions\n",
            machine.name, machine.state_count, machine.event_count, machine.transition_count);

    if (ensure_output_dir(outdir) != 0) {
        return 1;
    }

    if (generate_sm_h(outdir, prefix) != 0) return 1;
    if (generate_sm_c(outdir, prefix) != 0) return 1;

    generate_version(outdir, profile);

    return 0;
}
