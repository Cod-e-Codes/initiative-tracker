/*
 * D&D Initiative Tracker
 *
 * Compile: gcc initiative.c -lncurses -o initiative
 * Run: ./initiative
 */

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

#define MAX_COMBATANTS 50
#define NAME_LENGTH 32
#define NUM_CONDITIONS 15
#define SAVE_FILE_NAME ".dnd_tracker_save.txt"
#define LOG_EXPORT_FILE_NAME "combat_log_export.txt"
#define MAX_MESSAGE_QUEUE 5
#define MESSAGE_DISPLAY_TIME 1500 /* milliseconds */
#define MESSAGE_DISPLAY_DURATION_SECONDS 1.5 /* seconds - matches MESSAGE_DISPLAY_TIME */

/* Application modes */
typedef enum {
    MODE_COMBAT = 0,
    MODE_CONDITIONS = 1,
    MODE_HELP = 2
} AppMode;

/* Conditions as bit flags - synchronized with condition_data array */
typedef enum {
    COND_BLINDED       = (1 << 0),
    COND_CHARMED       = (1 << 1),
    COND_DEAFENED      = (1 << 2),
    COND_FRIGHTENED    = (1 << 3),
    COND_GRAPPLED      = (1 << 4),
    COND_INCAPACITATED = (1 << 5),
    COND_POISONED      = (1 << 6),
    COND_PRONE         = (1 << 7),
    COND_RESTRAINED    = (1 << 8),
    COND_STUNNED       = (1 << 9),
    COND_INVISIBLE     = (1 << 10),
    COND_PARALYZED     = (1 << 11),
    COND_PETRIFIED     = (1 << 12),
    COND_UNCONSCIOUS   = (1 << 13),
    COND_EXHAUSTION    = (1 << 14)
} Condition;

/* Condition metadata structure to keep enum and names synchronized */
typedef struct {
    Condition bitmask;
    const char* name;
} ConditionInfo;

/* Condition data array - ensures enum and names stay in sync */
static const ConditionInfo condition_data[NUM_CONDITIONS] = {
    {COND_BLINDED,       "Blinded"},
    {COND_CHARMED,       "Charmed"},
    {COND_DEAFENED,      "Deafened"},
    {COND_FRIGHTENED,    "Frightened"},
    {COND_GRAPPLED,      "Grappled"},
    {COND_INCAPACITATED, "Incapacitated"},
    {COND_POISONED,      "Poisoned"},
    {COND_PRONE,         "Prone"},
    {COND_RESTRAINED,    "Restrained"},
    {COND_STUNNED,       "Stunned"},
    {COND_INVISIBLE,     "Invisible"},
    {COND_PARALYZED,     "Paralyzed"},
    {COND_PETRIFIED,     "Petrified"},
    {COND_UNCONSCIOUS,   "Unconscious"},
    {COND_EXHAUSTION,    "Exhaustion"}
};

/* Forward declaration */
const char* get_condition_name(int index);

typedef enum {
    TYPE_PLAYER = 0,
    TYPE_ENEMY = 1
} CombatantType;

typedef struct {
    int id;
    char name[NAME_LENGTH];
    int initiative;
    int dex;
    int max_hp;
    int hp;
    CombatantType type;
    uint16_t conditions;
    int condition_duration[NUM_CONDITIONS];
    /* Death Save Tracking */
    int death_save_successes;
    int death_save_failures;
    int is_stable;  /* 1 if stable at 0 HP, 0 otherwise */
    int is_dead;    /* 1 if dead, 0 otherwise */
} Combatant;

/* Log Entry Structure */
typedef struct {
    int round;
    int turn_id;
    time_t timestamp;
    char message[128];
} CombatLogEntry;

/* Undo State Structure - Stores a snapshot of key data for undo */
typedef struct {
    Combatant combatants[MAX_COMBATANTS];
    int count;
    int current_turn_id;
    int selected_id;
    int round;
} UndoState;

#define MAX_UNDO_STACK 10 /* Keep the last 10 states */

/* Message Queue Structure */
typedef struct {
    char text[128];
    int is_error;
    time_t timestamp;
} MessageQueueEntry;

typedef struct {
    Combatant combatants[MAX_COMBATANTS];
    int count;
    int current_turn_id;
    int selected_id;
    int round;
    int next_id;

    /* Combat Log */
    CombatLogEntry* combat_log;
    int log_count;
    int log_capacity;

    /* Undo Stack */
    UndoState undo_stack[MAX_UNDO_STACK];
    int undo_count;

    /* Message Queue */
    MessageQueueEntry message_queue[MAX_MESSAGE_QUEUE];
    int message_queue_count;

    /* App State */
    AppMode mode;
    int condition_menu_cursor;      /* Selected condition in menu */
    int condition_menu_target_id;   /* ID of combatant being edited */
    int scroll_offset;               /* Scroll offset for long lists */
} GameState;

/* Color pairs */
enum {
    COLOR_DEFAULT = 1,
    COLOR_ACTIVE_ROW,
    COLOR_SELECTED_ROW,
    COLOR_NAME_PLAYER,
    COLOR_NAME_ENEMY,
    COLOR_HP_GOOD,
    COLOR_HP_HURT,
    COLOR_HP_CRITICAL,
    COLOR_HP_UNCONSCIOUS,
    COLOR_DEAD,
    COLOR_HEADER,
    COLOR_SEPARATOR,
    COLOR_MENU_SEL,
    COLOR_MENU_NORM,
    COLOR_MSG_SUCCESS,
    COLOR_MSG_ERROR
};

/* Prototypes */
void init_colors(void);
void init_log(GameState* state);
void cleanup_log(GameState* state);
void draw_ui(GameState* state);
void draw_filtered_list(GameState* state, int start_y, int start_x, int width, int height, CombatantType type);
void add_combatant(GameState* state);
void remove_combatant(GameState* state);
void edit_hp(GameState* state);
void reroll_initiative(GameState* state);
void toggle_condition(GameState* state);
void next_turn(GameState* state);
void prev_turn(GameState* state);
void move_selection(GameState* state, int direction);
void sort_combatants(GameState* state);
int compare_combatants(const void* a, const void* b);
void decrement_condition_durations(GameState* state);
void save_state(GameState* state);
void load_state(GameState* state);
void roll_death_save(GameState* state, Combatant* c);
void reset_death_saves(Combatant* c);
void handle_damage_at_zero_hp(GameState* state, Combatant* c, int damage, int is_critical);
void stabilize_combatant(GameState* state);
void draw_condition_menu(GameState* state);
int handle_condition_menu_input(GameState* state, int ch);
void draw_help_menu(GameState* state);
void duplicate_combatant(GameState* state);

/* New Feature Prototypes */
void log_action(GameState* state, const char* format, ...);
void export_log(GameState* state);
void save_undo_state(GameState* state);
void undo_last_action(GameState* state);

/* Helper Prototypes */
int get_input_string(const char* prompt, char* buffer, int max_len);
int get_input_int(GameState* state, const char* prompt, int* value, int min_val, int max_val);
int get_input_char(const char* prompt, const char* allowed);
int get_input_confirm(const char* prompt);
void show_message(GameState* state, const char* msg, int is_error);
void draw_message_queue(GameState* state);
void clear_old_messages(GameState* state);
int get_index_by_id(GameState* state, int id);
int parse_int_safe(const char* str, int* out);

int main(void) {
    GameState state = {0};
    state.round = 1;
    state.next_id = 1;
    state.current_turn_id = -1;
    state.selected_id = -1;
    state.message_queue_count = 0;
    state.mode = MODE_COMBAT;
    state.condition_menu_cursor = 0;
    state.condition_menu_target_id = -1;
    state.scroll_offset = 0;
    init_log(&state);
    srand((unsigned int)time(NULL));

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    /* Blocking input prevents UI flicker from constant redraws */
    timeout(-1);

    if (has_colors()) {
        start_color();
        init_colors();
    }

    int running = 1;

    while (running) {
        clear_old_messages(&state);
        draw_ui(&state);

        /* Periodic timeout to check message expiration without blocking */
        timeout(1000);
        int ch = getch();
        timeout(-1);

        if (ch == ERR) {
            /* Timeout occurred - check if messages expired and redraw if needed */
            int old_count = state.message_queue_count;
            clear_old_messages(&state);
            if (state.message_queue_count != old_count) {
                continue;
            }
            continue;
        }

        if (state.mode == MODE_CONDITIONS) {
            /* ESC closes menu immediately, bypassing handler */
            if (ch == 27) {
                state.mode = MODE_COMBAT;
                show_message(&state, "Condition menu closed.", 0);
                continue;
            }
            handle_condition_menu_input(&state, ch);
            continue;
        } else if (state.mode == MODE_HELP) {
            state.mode = MODE_COMBAT;
            continue;
        }
        switch (tolower(ch)) {
            case 'q': running = 0; break;
            case 'a': save_undo_state(&state); add_combatant(&state); break;
            case 'd': if (state.count > 0) { save_undo_state(&state); remove_combatant(&state); } break;
            case 'h':
                if (state.count > 0) {
                    save_undo_state(&state);
                    edit_hp(&state);
                }
                break;
            case '?':
                state.mode = MODE_HELP;
                break;
            case 'r': if (state.count > 0) { save_undo_state(&state); reroll_initiative(&state); } break;
            case 'c': if (state.count > 0) toggle_condition(&state); break;
            case 'n': if (state.count > 0) { save_undo_state(&state); next_turn(&state); } break;
            case 'p': if (state.count > 0) { save_undo_state(&state); prev_turn(&state); } break;
            case 's': save_state(&state); break;
            case 'l': load_state(&state); break;
            case 'e': if (state.log_count > 0) export_log(&state); break;
            case 'z': undo_last_action(&state); break;
            case 'x': if (state.count > 0) { save_undo_state(&state); roll_death_save(&state, NULL); } break;
            case 't': if (state.count > 0) { save_undo_state(&state); stabilize_combatant(&state); } break;
            case 'u': if (state.count > 0) { save_undo_state(&state); duplicate_combatant(&state); } break;
            case KEY_UP:
            case 'k':
                if (state.count > 0) move_selection(&state, -1);
                break;
            case KEY_DOWN:
            case 'j':
                if (state.count > 0) move_selection(&state, 1);
                break;
        }
    }

    cleanup_log(&state);
    endwin();
    return 0;
}

void init_colors(void) {
    init_pair(COLOR_DEFAULT, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_ACTIVE_ROW, COLOR_BLACK, COLOR_GREEN);
    init_pair(COLOR_SELECTED_ROW, COLOR_BLACK, COLOR_CYAN);
    init_pair(COLOR_NAME_PLAYER, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_NAME_ENEMY, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_HP_GOOD, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_HP_HURT, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_HP_CRITICAL, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_HP_UNCONSCIOUS, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_DEAD, COLOR_WHITE, COLOR_RED);
    init_pair(COLOR_HEADER, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_SEPARATOR, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_MENU_SEL, COLOR_BLACK, COLOR_YELLOW);
    init_pair(COLOR_MENU_NORM, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_MSG_SUCCESS, COLOR_BLACK, COLOR_GREEN);
    init_pair(COLOR_MSG_ERROR, COLOR_WHITE, COLOR_RED);
}

 /* --- Logging Functions --- */

void init_log(GameState* state) {
    state->log_capacity = 10;
    state->combat_log = (CombatLogEntry*)calloc((size_t)state->log_capacity, sizeof(CombatLogEntry));
    if (!state->combat_log) {
        /* Allocation failure: disable logging gracefully */
        state->log_capacity = 0;
        state->log_count = 0;
    }
}

void cleanup_log(GameState* state) {
    if (state->combat_log) {
        free(state->combat_log);
        state->combat_log = NULL;
    }
    state->log_count = 0;
    state->log_capacity = 0;
}

void log_action(GameState* state, const char* format, ...) {
    if (!state->combat_log) return;

    if (state->log_count >= state->log_capacity) {
        int new_capacity = state->log_capacity * 2;
        CombatLogEntry* new_log = (CombatLogEntry*)realloc(state->combat_log, (size_t)new_capacity * sizeof(CombatLogEntry));

         if (!new_log) return; /* Realloc failed - skip logging rather than crash */

        state->combat_log = new_log;
        state->log_capacity = new_capacity;
    }

    CombatLogEntry* entry = &state->combat_log[state->log_count];
    entry->round = state->round;
    entry->turn_id = state->current_turn_id;
    entry->timestamp = time(NULL);

    va_list args;
    va_start(args, format);
    vsnprintf(entry->message, sizeof(entry->message), format, args);
    va_end(args);

    state->log_count++;
}

/**
 * Export combat log to a text file.
 * Appends to existing file to preserve session history.
 */
void export_log(GameState* state) {
    if (!state || state->log_count == 0) {
        show_message(state, "No log entries to export!", 1);
        return;
    }

    char path[256];
    const char* home = getenv("HOME");
    if (home) {
        int ret = snprintf(path, sizeof(path), "%s/%s", home, LOG_EXPORT_FILE_NAME);
        if (ret < 0 || ret >= (int)sizeof(path)) {
            show_message(state, "Error: Path too long for log file!", 1);
            return;
        }
    } else {
        snprintf(path, sizeof(path), "%s", LOG_EXPORT_FILE_NAME);
    }

    FILE* f = fopen(path, "a");
    if (!f) {
        char err_msg[256];
        char path_display[200];
        size_t path_len = strlen(path);
        if (path_len >= sizeof(path_display)) {
            /* Show last part of path if too long */
            snprintf(path_display, sizeof(path_display), "...%s", path + (path_len - (sizeof(path_display) - 4)));
        } else {
            strncpy(path_display, path, sizeof(path_display) - 1);
            path_display[sizeof(path_display) - 1] = '\0';
        }
        snprintf(err_msg, sizeof(err_msg), "Log export failed! Cannot open: %s", path_display);
        show_message(state, err_msg, 1);
        return;
    }

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(f, "================================================\n");
    fprintf(f, "COMBAT LOG EXPORT: %s\n", time_str);
    fprintf(f, "================================================\n");

    for (int i = 0; i < state->log_count; i++) {
        CombatLogEntry* entry = &state->combat_log[i];
        fprintf(f, "[R%d] %s\n", entry->round, entry->message);
    }

    fprintf(f, "--- END OF LOG ---\n\n");
    fclose(f);

    state->log_count = 0;
    show_message(state, "Log Exported and Cleared!", 0);
}

 /* --- Undo Functions --- */

void save_undo_state(GameState* state) {
    /* Circular buffer: drop oldest when full */
    if (state->undo_count >= MAX_UNDO_STACK) {
        memmove(&state->undo_stack[0], &state->undo_stack[1], (MAX_UNDO_STACK - 1) * sizeof(UndoState));
        state->undo_count = MAX_UNDO_STACK - 1;
    }

    UndoState* current_undo = &state->undo_stack[state->undo_count];
    memcpy(current_undo->combatants, state->combatants, (size_t)state->count * sizeof(Combatant));
    current_undo->count = state->count;
    current_undo->current_turn_id = state->current_turn_id;
    current_undo->selected_id = state->selected_id;
    current_undo->round = state->round;

    state->undo_count++;
}

void undo_last_action(GameState* state) {
    if (state->undo_count == 0) {
        show_message(state, "Nothing to undo!", 1);
        return;
    }

    state->undo_count--;
    UndoState* prev_state = &state->undo_stack[state->undo_count];

    state->count = prev_state->count;
    memcpy(state->combatants, prev_state->combatants, (size_t)state->count * sizeof(Combatant));
    state->current_turn_id = prev_state->current_turn_id;
    state->selected_id = prev_state->selected_id;
    state->round = prev_state->round;

    log_action(state, "Action UNDONE. Reverted to start of Round %d.", state->round);

    show_message(state, "Undo successful!", 0);
}

 /* --- TUI/Core Functions --- */

void draw_ui(GameState* state) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    /* erase() is faster than clear() - doesn't force full screen refresh */
    erase();

    clear_old_messages(state);

    attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    mvhline(0, 0, ' ', cols);
    mvprintw(0, 1, "D&D INITIATIVE TRACKER | Round: %d", state->round);
    mvhline(1, 0, ' ', cols);
    mvprintw(1, 1, "Keys: A(dd) D(el) H(eal) C(ond) N(ext) P(rev) R(eroll) U(dup) X(death) T(stabilize)");
    mvhline(2, 0, ' ', cols);
    mvprintw(2, 1, "      E(xport) Z(undo) S(ave) L(oad) ?(help) Q(uit)");
    attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);

    int split_y = rows / 2;
    int list_height = split_y - 5;

    mvhline(3, 0, ACS_HLINE, cols);
    attron(A_BOLD);
    mvprintw(3, 2, "[ PLAYERS ]");
    attroff(A_BOLD);
    draw_filtered_list(state, 4, 0, cols, list_height, TYPE_PLAYER);

    attron(COLOR_PAIR(COLOR_SEPARATOR));
    mvhline(split_y, 0, ACS_HLINE, cols);
    attroff(COLOR_PAIR(COLOR_SEPARATOR));

    attron(A_BOLD);
    mvprintw(split_y, 2, "[ ENEMIES ]");
    attroff(A_BOLD);

    draw_filtered_list(state, split_y + 1, 0, cols, rows - split_y - 1, TYPE_ENEMY);

    if (state->mode == MODE_CONDITIONS) {
        draw_condition_menu(state);
    } else if (state->mode == MODE_HELP) {
        draw_help_menu(state);
    }

    draw_message_queue(state);

    refresh();
}

void draw_filtered_list(GameState* state, int start_y, int start_x, int width, int height, CombatantType type) {
    if (state->count == 0) return;

    /* Column Headers */
    attron(A_UNDERLINE);
    mvprintw(start_y, start_x + 2, "%-20s %4s %4s %8s %12s %s", "Name", "Init", "Dex", "HP", "Death Saves", "Conditions");
    attroff(A_UNDERLINE);

    int type_count = 0;
    int selected_visual_index = -1;
    int active_visual_index = -1;

    int visual_map[MAX_COMBATANTS];

    for (int i = 0; i < state->count; i++) {
        if (state->combatants[i].type == type) {
            visual_map[type_count] = i;
            if (state->combatants[i].id == state->selected_id) selected_visual_index = type_count;
            if (state->combatants[i].id == state->current_turn_id) active_visual_index = type_count;
            type_count++;
        }
    }

    if (type_count == 0) {
        mvprintw(start_y + 2, start_x + 2, "(None)");
        return;
    }

    int list_display_h = height - 1;
    int scroll_offset = 0;

    if (selected_visual_index != -1) {
        if (selected_visual_index >= list_display_h) {
            scroll_offset = selected_visual_index - list_display_h + 1;
        }
    } else {
        if (active_visual_index != -1 && active_visual_index >= list_display_h) {
            scroll_offset = active_visual_index - list_display_h + 1;
        }
    }

    int y = start_y + 1;
    for (int v = scroll_offset; v < type_count && y < start_y + height; v++) {
        int real_idx = visual_map[v];
        Combatant* c = &state->combatants[real_idx];

        int row_color = COLOR_DEFAULT;
        int attrs = 0;

        if (c->id == state->selected_id) {
            row_color = COLOR_SELECTED_ROW;
        }

        if (c->id == state->current_turn_id) {
            attrs = A_BOLD;
            mvprintw(y, start_x, ">");
            if (c->id == state->selected_id) row_color = COLOR_ACTIVE_ROW;
        }

        attron(COLOR_PAIR(row_color) | (unsigned int)attrs);
        mvprintw(y, start_x + 2, "%-20s %4d %4d", c->name, c->initiative, c->dex);
        attroff(COLOR_PAIR(row_color) | (unsigned int)attrs);

        /* Color coding: Good > Hurt > Critical > Unconscious/Dead */
        int hp_color = COLOR_HP_GOOD;

        if (c->hp <= 0 || c->is_dead) {
            /* 5e rule: enemies die at 0 HP, players go unconscious */
            if (c->type == TYPE_ENEMY || c->is_dead) hp_color = COLOR_DEAD;
            else hp_color = COLOR_HP_UNCONSCIOUS;
        }
        else if (c->hp <= c->max_hp / 4) hp_color = COLOR_HP_CRITICAL;
        else if (c->hp <= c->max_hp / 2) hp_color = COLOR_HP_HURT;

        attron(COLOR_PAIR(hp_color));
        if (c->is_dead) {
            mvprintw(y, start_x + 33, "%s", " DEAD  ");
        } else if (c->hp <= 0 && c->type == TYPE_PLAYER) {
            mvprintw(y, start_x + 33, "%s", " DOWN  ");
        } else if (c->hp <= 0 && c->type == TYPE_ENEMY) {
            mvprintw(y, start_x + 33, "%s", " DEAD  ");
        } else {
            mvprintw(y, start_x + 33, "%3d/%3d", c->hp, c->max_hp);
        }
        attroff(COLOR_PAIR(hp_color));

        /* Death Saves Display (only for players at 0 HP) */
        if (c->type == TYPE_PLAYER && c->hp <= 0 && !c->is_dead) {
            if (c->is_stable) {
                mvprintw(y, start_x + 42, "STABLE");
            } else {
                char ds_str[16];
                snprintf(ds_str, sizeof(ds_str), "S:%d F:%d", c->death_save_successes, c->death_save_failures);
                mvprintw(y, start_x + 42, "%-12s", ds_str);
            }
        } else if (c->is_dead) {
            attron(COLOR_PAIR(COLOR_DEAD));
            mvprintw(y, start_x + 42, "DEAD");
            attroff(COLOR_PAIR(COLOR_DEAD));
        } else {
            mvprintw(y, start_x + 42, "            ");
        }

        /* Build condition string with bounds checking to prevent overflow */
        char cond_str[128] = "";
        size_t cond_str_len = 0;
        for (int j = 0; j < NUM_CONDITIONS; j++) {
            if (c->conditions & (1 << j)) {
                char temp[32];
                int temp_len;
                if (c->condition_duration[j] > 0)
                    temp_len = snprintf(temp, sizeof(temp), "%s(%d) ", get_condition_name(j), c->condition_duration[j]);
                else
                    temp_len = snprintf(temp, sizeof(temp), "%s ", get_condition_name(j));

                if (cond_str_len + (size_t)temp_len < sizeof(cond_str) - 1) {
                    strncat(cond_str, temp, sizeof(cond_str) - cond_str_len - 1);
                    cond_str_len += (size_t)temp_len;
                } else {
                    break; /* Buffer full - truncate gracefully */
                }
            }
        }
        int remaining_w = width - 55;
        if (remaining_w > 0)
            mvprintw(y, start_x + 54, "%.*s", remaining_w, cond_str);

        y++;
    }

    if (type_count > list_display_h && scroll_offset + list_display_h < type_count) {
        int indicator_y = start_y + height - 1;
        attron(A_BOLD);
        mvprintw(indicator_y, start_x + 2, "(%d more \u2193)", type_count - (scroll_offset + list_display_h));
        attroff(A_BOLD);
    }
}

/**
 * Open condition menu for selected combatant.
 * Switches to MODE_CONDITIONS for interactive editing.
 */
void toggle_condition(GameState* state) {
    if (state->count == 0) return;
    int idx = get_index_by_id(state, state->selected_id);
    if (idx == -1) {
        show_message(state, "No combatant selected!", 1);
        return;
    }

    save_undo_state(state);
    state->mode = MODE_CONDITIONS;
    state->condition_menu_cursor = 0;
    state->condition_menu_target_id = state->selected_id;
}

/**
 * Draw the condition sub-menu overlay.
 */
void draw_condition_menu(GameState* state) {
    int idx = get_index_by_id(state, state->condition_menu_target_id);
    if (idx == -1) {
        state->mode = MODE_COMBAT;
        return;
    }

    Combatant* c = &state->combatants[idx];
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int menu_height = NUM_CONDITIONS + 6;
    int menu_width = 60;
    int start_y = (rows - menu_height) / 2;
    int start_x = (cols - menu_width) / 2;
    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    /* Draw overlay background */
    attron(COLOR_PAIR(COLOR_HEADER));
    for (int y = start_y; y < start_y + menu_height && y < rows; y++) {
        for (int x = start_x; x < start_x + menu_width && x < cols; x++) {
            mvaddch(y, x, ' ');
        }
    }
    attroff(COLOR_PAIR(COLOR_HEADER));

    attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    mvprintw(start_y, start_x + 2, "Conditions for: %s", c->name);
    attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);

    attron(COLOR_PAIR(COLOR_HEADER));
    mvhline(start_y + 1, start_x, ACS_HLINE, menu_width);

    attron(COLOR_PAIR(COLOR_HEADER) | A_DIM);
    mvprintw(start_y + 2, start_x + 2, "UP/DOWN: Navigate | ENTER: Toggle | 'd': Duration");
    attroff(COLOR_PAIR(COLOR_HEADER) | A_DIM);

    attron(COLOR_PAIR(COLOR_HEADER));
    mvhline(start_y + 3, start_x, ACS_HLINE, menu_width);

    for (int i = 0; i < NUM_CONDITIONS; i++) {
        int is_active = c->conditions & (1 << i);
        int is_selected = (i == state->condition_menu_cursor);
        int y = start_y + 4 + i;

        int pair = is_selected ? COLOR_MENU_SEL : COLOR_MENU_NORM;
        attron(COLOR_PAIR(pair));
        if (is_selected) attron(A_BOLD);

        mvprintw(y, start_x + 2, "[%c] %-20s", is_active ? 'X' : ' ', get_condition_name(i));

        if (is_active && c->condition_duration[i] > 0) {
            printw(" (%d rounds)", c->condition_duration[i]);
        }

        if (is_selected) attroff(A_BOLD);
        attroff(COLOR_PAIR(pair));
    }

    attron(COLOR_PAIR(COLOR_HEADER));
    mvhline(start_y + menu_height - 2, start_x, ACS_HLINE, menu_width);
    mvprintw(start_y + menu_height - 1, start_x + (menu_width - 20) / 2, "ESC or 'q' to close");
    attroff(COLOR_PAIR(COLOR_HEADER));
}

/**
 * Handle input in condition menu mode.
 */
int handle_condition_menu_input(GameState* state, int ch) {
    int idx = get_index_by_id(state, state->condition_menu_target_id);
    if (idx == -1) {
        state->mode = MODE_COMBAT;
        return 1;
    }

    Combatant* c = &state->combatants[idx];

    switch (ch) {
        case KEY_UP:
        case 'k':
            state->condition_menu_cursor = (state->condition_menu_cursor - 1 + NUM_CONDITIONS) % NUM_CONDITIONS;
            return 1;
        case KEY_DOWN:
        case 'j':
            state->condition_menu_cursor = (state->condition_menu_cursor + 1) % NUM_CONDITIONS;
            return 1;
        case '\n':
        case '\r':
        case ' ':
            {
                int cursor = state->condition_menu_cursor;
                int was_active = c->conditions & (1 << cursor);
                c->conditions ^= (1 << cursor);

                if (!was_active && (c->conditions & (1 << cursor))) {
                    log_action(state, "%s: %s applied.", c->name, get_condition_name(cursor));
                } else if (was_active && !(c->conditions & (1 << cursor))) {
                    c->condition_duration[cursor] = 0;
                    log_action(state, "%s: %s removed.", c->name, get_condition_name(cursor));
                }
            }
            return 1;
        case 'd':
        case 'D':
            {
                int cursor = state->condition_menu_cursor;
                if (c->conditions & (1 << cursor)) {
                    int dur;
                    if (get_input_int(state, "Duration (rounds, 0=permanent): ", &dur, 0, INT_MAX)) {
                        c->condition_duration[cursor] = dur;
                        log_action(state, "%s: %s duration set to %d.", c->name, get_condition_name(cursor), dur);
                    }
                } else {
                    show_message(state, "Enable condition first!", 1);
                }
            }
            return 1;
        case 'q':
        case 'Q':
        case 27: /* ESC */
            state->mode = MODE_COMBAT;
            show_message(state, "Condition menu closed.", 0);
            return 1;
        default:
            return 0;
    }
}

/**
 * Draw help menu overlay.
 */
void draw_help_menu(GameState* state) {
    (void)state;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int h_height = 28;
    int h_width = 75;
    int h_start_y = (rows - h_height) / 2;
    int h_start_x = (cols - h_width) / 2;
    if (h_start_y < 0) h_start_y = 0;
    if (h_start_x < 0) h_start_x = 0;

    attron(COLOR_PAIR(COLOR_HEADER));
    for (int y = h_start_y; y < h_start_y + h_height && y < rows; y++) {
        for (int x = h_start_x; x < h_start_x + h_width && x < cols; x++) {
            mvaddch(y, x, ' ');
        }
    }
    attroff(COLOR_PAIR(COLOR_HEADER));

    attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    mvprintw(h_start_y, h_start_x + (h_width - 12) / 2, "HELP & COMMANDS");
    attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);

    attron(COLOR_PAIR(COLOR_HEADER));
    mvhline(h_start_y + 1, h_start_x, ACS_HLINE, h_width);

    attron(COLOR_PAIR(COLOR_HEADER));
    int y = h_start_y + 3;
    mvprintw(y++, h_start_x + 2, "Navigation:");
    mvprintw(y++, h_start_x + 4, "UP/DOWN or k/j : Move selection");
    mvprintw(y++, h_start_x + 4, "ENTER : Set selected as current turn");
    y++;
    mvprintw(y++, h_start_x + 2, "Combat Commands:");
    mvprintw(y++, h_start_x + 4, "A : Add combatant");
    mvprintw(y++, h_start_x + 4, "D : Delete selected combatant");
    mvprintw(y++, h_start_x + 4, "H : Edit HP (damage/heal)");
    mvprintw(y++, h_start_x + 4, "C : Toggle conditions (interactive menu)");
    mvprintw(y++, h_start_x + 4, "N : Next turn (auto death saves)");
    mvprintw(y++, h_start_x + 4, "P : Previous turn");
    mvprintw(y++, h_start_x + 4, "R : Reroll initiative");
    mvprintw(y++, h_start_x + 4, "U : Duplicate selected combatant");
    mvprintw(y++, h_start_x + 4, "X : Manual death save roll");
    mvprintw(y++, h_start_x + 4, "T : Stabilize combatant");
    y++;
    mvprintw(y++, h_start_x + 2, "Other:");
    mvprintw(y++, h_start_x + 4, "Z : Undo last action");
    mvprintw(y++, h_start_x + 4, "E : Export combat log");
    mvprintw(y++, h_start_x + 4, "S : Save game");
    mvprintw(y++, h_start_x + 4, "L : Load game");
    mvprintw(y++, h_start_x + 4, "Q : Quit");
    attroff(COLOR_PAIR(COLOR_HEADER));

    attron(COLOR_PAIR(COLOR_HEADER));
    mvhline(h_start_y + h_height - 2, h_start_x, ACS_HLINE, h_width);
    mvprintw(h_start_y + h_height - 1, h_start_x + (h_width - 20) / 2, "Press any key to close");
    attroff(COLOR_PAIR(COLOR_HEADER));

    /* Safety check: clear overflow if content exceeds menu height */
    if (y > h_start_y + h_height - 2) {
        for (int clear_y = h_start_y + h_height - 2; clear_y < h_start_y + h_height; clear_y++) {
            if (clear_y < rows) {
                move(clear_y, h_start_x);
                clrtoeol();
            }
        }
    }
}


/**
 * Add a new combatant to the initiative tracker.
 * Validates all inputs and ensures data integrity.
 */
void add_combatant(GameState* state) {
    if (!state) return;

    if (state->count >= MAX_COMBATANTS) {
        show_message(state, "List full! Maximum combatants reached.", 1);
        return;
    }

    Combatant c = {0};
    int type_char = get_input_char("Type? (P)layer / (E)nemy: ", "pePE");
    if (type_char == 0) return; /* User cancelled */

    c.type = (tolower(type_char) == 'p') ? TYPE_PLAYER : TYPE_ENEMY;

    if (!get_input_string("Name: ", c.name, NAME_LENGTH)) return;

    /* Trim trailing whitespace and validate non-empty */
    int name_len = (int)strlen(c.name);
    while (name_len > 0 && isspace((unsigned char)c.name[name_len - 1])) {
        c.name[--name_len] = '\0';
    }
    if (name_len == 0) {
        show_message(state, "Name cannot be empty!", 1);
        return;
    }

    if (!get_input_int(state, "Initiative: ", &c.initiative, INT_MIN, INT_MAX)) return;
    /* Warn on unusual values (likely input errors) */
    if (c.initiative < -10 || c.initiative > 50) {
        show_message(state, "Warning: Initiative seems unusual. Continuing anyway.", 1);
    }

    if (!get_input_int(state, "Dexterity (Tiebreaker): ", &c.dex, INT_MIN, INT_MAX)) return;
    if (c.dex < -10 || c.dex > 20) {
        show_message(state, "Warning: Dex modifier seems unusual. Continuing anyway.", 1);
    }

    int max_hp;
    if (!get_input_int(state, "Max HP: ", &max_hp, 1, INT_MAX)) return;

    if (max_hp > 10000) {
        show_message(state, "Warning: Max HP seems unusually high. Continuing anyway.", 1);
    }

    c.max_hp = max_hp;
    c.hp = c.max_hp;
    c.death_save_successes = 0;
    c.death_save_failures = 0;
    c.is_stable = 0;
    c.is_dead = 0;

    if (state->next_id == INT_MAX) {
        state->next_id = 1;
    }
    c.id = state->next_id++;

    state->combatants[state->count++] = c;
    state->selected_id = c.id;
    if (state->count == 1) state->current_turn_id = c.id;

    sort_combatants(state);

    /* Round 1 edge case: ensure turn starts with highest initiative */
    if (state->round == 1 && state->count > 0) {
        state->current_turn_id = state->combatants[0].id;
    }

    log_action(state, "Added %s: Init %d, HP %d.", c.name, c.initiative, c.max_hp);
}

/**
 * Duplicates the selected combatant.
 *
 * Logic:
 * - Prompts user for the number of copies to create.
 * - Naming: Finds the base name (e.g., "Goblin" from "Goblin 5")
 * renames the original to "BaseName 1", and names copies sequentially
 * ("BaseName 2", "BaseName 3", etc.), ensuring no name collisions.
 * - Initiative: Rerolls initiative for each duplicate (1d20 + Dex Modifier).
 * - State: Duplicates are considered "fresh spawns" (Max HP, no conditions,
 * no death saves).
 *
 * @param state Pointer to the current GameState.
 */
void duplicate_combatant(GameState* state) {
    if (state->count == 0) return;

    int idx = get_index_by_id(state, state->selected_id);
    if (idx == -1) {
        show_message(state, "No combatant selected!", 1);
        return;
    }

    int max_copies = MAX_COMBATANTS - state->count;
    if (max_copies <= 0) {
        show_message(state, "List full! Cannot duplicate.", 1);
        return;
    }

    Combatant* source = &state->combatants[idx];

    int num_copies;
    if (!get_input_int(state, "Number of duplicates: ", &num_copies, 1, max_copies)) {
        return;
    }

    /* Reserve space for suffix to prevent truncation warnings */
    const int max_suffix_len = 12;
    const int max_base_len = NAME_LENGTH - max_suffix_len;

    char base_name[NAME_LENGTH];
    /* Strip trailing numbers to avoid "Goblin 1 1" scenarios */
    int len = (int)strlen(source->name);
    int trailing_num_start = len;

    while (trailing_num_start > 0 && isdigit((unsigned char)source->name[trailing_num_start - 1])) {
        trailing_num_start--;
    }

    if (trailing_num_start < len) {
        int base_end = trailing_num_start;
        if (base_end > 0 && isspace((unsigned char)source->name[base_end - 1])) {
            base_end--;
        }
        strncpy(base_name, source->name, (size_t)base_end);
        base_name[base_end] = '\0';
    } else {
        strncpy(base_name, source->name, NAME_LENGTH);
        base_name[NAME_LENGTH - 1] = '\0';
    }

    int base_len = (int)strlen(base_name);
    while (base_len > 0 && isspace((unsigned char)base_name[base_len - 1])) {
        base_name[--base_len] = '\0';
    }

    if (base_len > max_base_len) {
        base_name[max_base_len] = '\0';
        base_len = max_base_len;
    }

    /* Find highest existing number to avoid duplicates */
    int highest_num = 0;
    int base_name_len = (int)strlen(base_name);
    int original_has_number = 0;

    for (int i = 0; i < state->count; i++) {
        const char* existing_name = state->combatants[i].name;
        int existing_len = (int)strlen(existing_name);

        if (existing_len > base_name_len &&
            strncmp(existing_name, base_name, (size_t)base_name_len) == 0) {

            if (existing_name[base_name_len] == ' ') {
                const char* num_start = existing_name + base_name_len + 1;
                char* endptr;
                long parsed_num = strtol(num_start, &endptr, 10);

                if (endptr != num_start && *endptr == '\0' && parsed_num > 0 && parsed_num <= INT_MAX) {
                    int num = (int)parsed_num;
                    if (num > highest_num) {
                        highest_num = num;
                    }
                    if (i == idx) {
                        original_has_number = 1;
                    }
                }
            }
        }
    }

    int start_num;
    if (!original_has_number) {
        snprintf(source->name, NAME_LENGTH, "%.*s 1", max_base_len, base_name);
        start_num = (highest_num > 0) ? highest_num + 1 : 2;
    } else {
        start_num = highest_num + 1;
    }

    for (int i = 0; i < num_copies; i++) {
        if (state->count >= MAX_COMBATANTS) break;

        Combatant c = *source;

        if (state->next_id == INT_MAX) {
            state->next_id = 1;
        }
        c.id = state->next_id++;

        snprintf(c.name, NAME_LENGTH, "%.*s %d", max_base_len, base_name, start_num + i);
        c.initiative = (rand() % 20) + 1 + c.dex;

        /* Reset to fresh spawn state */
        c.hp = c.max_hp;
        c.conditions = 0;
        memset(c.condition_duration, 0, sizeof(c.condition_duration));
        c.death_save_successes = 0;
        c.death_save_failures = 0;
        c.is_stable = 0;
        c.is_dead = 0;

        state->combatants[state->count++] = c;
    }

    sort_combatants(state);

    if (state->count > 0) {
        state->selected_id = state->combatants[state->count - 1].id;
    }

    log_action(state, "Created %d duplicates of %s.", num_copies, base_name);
    show_message(state, "Duplicates created.", 0);
}

void remove_combatant(GameState* state) {
    if (state->count == 0) return;

    int idx = get_index_by_id(state, state->selected_id);
    if (idx == -1) return;

    char prompt[128];
    snprintf(prompt, sizeof(prompt), "Delete %s? (y/n): ", state->combatants[idx].name);
    if (!get_input_confirm(prompt)) {
        return;
    }

    log_action(state, "Removed %s.", state->combatants[idx].name);

    if (state->current_turn_id == state->selected_id) {
        int next_idx = (idx + 1) % state->count;
        state->current_turn_id = (state->count > 1) ? state->combatants[next_idx].id : -1;
    }

    for (int i = idx; i < state->count - 1; i++) {
        state->combatants[i] = state->combatants[i + 1];
    }
    state->count--;

    if (state->count > 0) {
        if (idx >= state->count) idx = state->count - 1;
        state->selected_id = state->combatants[idx].id;
    } else {
        state->selected_id = -1;
        state->current_turn_id = -1;
        state->round = 1;
    }
}

/**
 * Edit HP for selected combatant with damage/healing.
 * Handles death saves, instant death, and unconscious state.
 */
void edit_hp(GameState* state) {
    if (!state) return;

    int idx = get_index_by_id(state, state->selected_id);
    if (idx == -1) {
        show_message(state, "No combatant selected!", 1);
        return;
    }

    Combatant* c = &state->combatants[idx];
    char prompt[64];
    snprintf(prompt, sizeof(prompt), "%s (%d/%d) Change (+/-): ", c->name, c->hp, c->max_hp);

    int change;
    if (get_input_int(state, prompt, &change, INT_MIN, INT_MAX)) {

        int old_hp = c->hp;
        int damage = (change < 0) ? -change : 0;

        /* 5e instant death rule: remaining damage >= max HP */
        if (damage > 0 && c->hp > 0 && (c->hp - damage) <= 0) {
            int remaining_damage = damage - c->hp;
            if (remaining_damage >= c->max_hp) {
                c->hp = 0;
                c->is_dead = 1;
                c->conditions |= COND_UNCONSCIOUS;
                reset_death_saves(c);
                show_message(state, "INSTANT DEATH!", 1);
                log_action(state, "%s died instantly (damage >= max HP).", c->name);
                return;
            }
        }

        c->hp += change;
        if (c->hp > c->max_hp) c->hp = c->max_hp;
        if (c->hp < 0) c->hp = 0;

        /* 5e rule: damage at 0 HP causes death save failures */
        if (damage > 0 && old_hp <= 0 && c->type == TYPE_PLAYER && !c->is_dead) {
            /* Critical hits within 5 feet cause 2 failures */
            int is_crit = 0;
            if (c->hp == 0) {
                char crit_prompt[128];
                snprintf(crit_prompt, sizeof(crit_prompt), "Critical hit? (y/n): ");
                is_crit = get_input_confirm(crit_prompt);
            }
            handle_damage_at_zero_hp(state, c, damage, is_crit);
        }

        if (change > 0) {
            log_action(state, "%s healed %d HP (%d/%d).", c->name, change, c->hp, c->max_hp);
        } else if (change < 0) {
            log_action(state, "%s took %d damage (%d/%d).", c->name, damage, c->hp, c->max_hp);
        }

        /* 5e rule: players go unconscious at 0 HP, not dead */
        if (c->type == TYPE_PLAYER) {
            if (c->hp == 0 && old_hp > 0) {
                if (!(c->conditions & COND_UNCONSCIOUS)) {
                    c->conditions |= COND_UNCONSCIOUS;
                    reset_death_saves(c);
                    show_message(state, "Player is DOWN! (Unconscious applied)", 1);
                    log_action(state, "%s is UNCONSCIOUS.", c->name);
                }
            } else if (c->hp > 0 && old_hp <= 0) {
                if (c->conditions & COND_UNCONSCIOUS) {
                    c->conditions &= (uint16_t)~COND_UNCONSCIOUS;
                    reset_death_saves(c);
                    c->is_stable = 0;
                    c->is_dead = 0;
                    show_message(state, "Player is UP! (Unconscious removed)", 0);
                    log_action(state, "%s is no longer unconscious.", c->name);
                }
            }
        }
    }
}

void reroll_initiative(GameState* state) {
    int idx = get_index_by_id(state, state->selected_id);
    if (idx == -1) return;

    int val;
    if (get_input_int(state, "New Init: ", &val, INT_MIN, INT_MAX)) {
        int old_init = state->combatants[idx].initiative;
        state->combatants[idx].initiative = val;
        sort_combatants(state);

        /* Round 1 edge case: ensure turn starts with highest initiative */
        if (state->round == 1 && state->count > 0) {
            state->current_turn_id = state->combatants[0].id;
        }

        log_action(state, "%s rerolled initiative from %d to %d.", state->combatants[idx].name, old_init, val);
    }
}

void next_turn(GameState* state) {
    if (state->count == 0) return;

    int idx = get_index_by_id(state, state->current_turn_id);
    if (idx == -1) idx = 0;
    else idx++;

    if (idx >= state->count) {
        idx = 0;
        state->round++;
        decrement_condition_durations(state);
        log_action(state, "--- START OF ROUND %d ---", state->round);
    }

    state->current_turn_id = state->combatants[idx].id;
    state->selected_id = state->current_turn_id;

    Combatant* c = &state->combatants[idx];
    log_action(state, "%s's turn.", c->name);

    /* 5e rule: death saves rolled at start of turn when at 0 HP */
    if (c->type == TYPE_PLAYER && c->hp <= 0 && !c->is_stable && !c->is_dead) {
        roll_death_save(state, c);
    }
}

void prev_turn(GameState* state) {
    if (state->count == 0) return;

    int idx = get_index_by_id(state, state->current_turn_id);
    if (idx == -1) idx = 0;
    else idx--;

    if (idx < 0) {
        idx = state->count - 1;
        if (state->round > 1) {
            state->round--;
            log_action(state, "--- END OF ROUND %d (Revert) ---", state->round);
        }
    }

    state->current_turn_id = state->combatants[idx].id;
    state->selected_id = state->current_turn_id;
    log_action(state, "Turn reverted to %s.", state->combatants[idx].name);
}

void decrement_condition_durations(GameState* state) {
    for (int i = 0; i < state->count; i++) {
        for (int j = 0; j < NUM_CONDITIONS; j++) {
            if (state->combatants[i].condition_duration[j] > 0) {
                state->combatants[i].condition_duration[j]--;
                if (state->combatants[i].condition_duration[j] == 0) {
                    state->combatants[i].conditions &= (uint16_t)~(1 << j);
                    log_action(state, "%s: %s duration ended.", state->combatants[i].name, get_condition_name(j));
                }
            }
        }
    }
}

void save_state(GameState* state) {
    char path[256];
    const char* home = getenv("HOME");
    if (home) {
        int ret = snprintf(path, sizeof(path), "%s/%s", home, SAVE_FILE_NAME);
        if (ret < 0 || ret >= (int)sizeof(path)) {
            show_message(state, "Error: Path too long for save file!", 1);
            return;
        }
    } else {
        snprintf(path, sizeof(path), "%s", SAVE_FILE_NAME);
    }

    FILE* f = fopen(path, "w");
    if (!f) {
        char err_msg[256];
        char path_display[200];
        size_t path_len = strlen(path);
        if (path_len >= sizeof(path_display)) {
            /* Show last part of path if too long */
            snprintf(path_display, sizeof(path_display), "...%s", path + (path_len - (sizeof(path_display) - 4)));
        } else {
            strncpy(path_display, path, sizeof(path_display) - 1);
            path_display[sizeof(path_display) - 1] = '\0';
        }
        snprintf(err_msg, sizeof(err_msg), "Save failed! Cannot open file: %s", path_display);
        show_message(state, err_msg, 1);
        return;
    }

    fprintf(f, "%d|%d|%d|%d|%d\n",
        state->round, state->next_id, state->count, state->current_turn_id, state->selected_id);

    for (int i = 0; i < state->count; i++) {
        Combatant* c = &state->combatants[i];
        int ret = fprintf(f, "%d|%s|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
            c->id, c->name, c->type, c->initiative, c->dex, c->max_hp, c->hp, c->conditions,
            c->death_save_successes, c->death_save_failures, c->is_stable, c->is_dead);

        if (ret < 0) {
            fclose(f);
            show_message(state, "Save failed! Write error occurred.", 1);
            return;
        }

        for(int j=0; j<NUM_CONDITIONS; j++) {
            ret = fprintf(f, "|%d", c->condition_duration[j]);
            if (ret < 0) {
                fclose(f);
                show_message(state, "Save failed! Write error occurred.", 1);
                return;
            }
        }
        ret = fprintf(f, "\n");
        if (ret < 0) {
            fclose(f);
            show_message(state, "Save failed! Write error occurred.", 1);
            return;
        }
    }

    if (fclose(f) != 0) {
        show_message(state, "Save failed! Error closing file.", 1);
        return;
    }
    show_message(state, "Game Saved.", 0);
}

void load_state(GameState* state) {
    if (state->count > 0 && !get_input_confirm("Loading will wipe current state. Are you sure? (y/n): ")) {
        return;
    }

    /* Standard load logic */
    char path[256];
    const char* home = getenv("HOME");
    if (home) snprintf(path, sizeof(path), "%s/%s", home, SAVE_FILE_NAME);
    else snprintf(path, sizeof(path), "%s", SAVE_FILE_NAME);

    FILE* f = fopen(path, "r");
    if (!f) {
        char err_msg[256];
        char path_display[200];
        size_t path_len = strlen(path);
        if (path_len >= sizeof(path_display)) {
            /* Show last part of path if too long */
            snprintf(path_display, sizeof(path_display), "...%s", path + (path_len - (sizeof(path_display) - 4)));
        } else {
            strncpy(path_display, path, sizeof(path_display) - 1);
            path_display[sizeof(path_display) - 1] = '\0';
        }
        snprintf(err_msg, sizeof(err_msg), "Load failed! Cannot open file: %s", path_display);
        show_message(state, err_msg, 1);
        return;
    }

     /* Clear state before loading to prevent partial data */
    state->log_count = 0;
    state->undo_count = 0;

    memset(state->combatants, 0, sizeof(state->combatants));

    char line[1024];
    if (fgets(line, sizeof(line), f)) {
        int parsed = sscanf(line, "%d|%d|%d|%d|%d",
            &state->round, &state->next_id, &state->count, &state->current_turn_id, &state->selected_id);
        if (parsed != 5) {
            fclose(f);
            show_message(state, "Load failed! Invalid save file format.", 1);
            return;
        }
        if (state->count < 0 || state->count > MAX_COMBATANTS) {
            fclose(f);
            show_message(state, "Load failed! Invalid combatant count in save file.", 1);
            return;
        }
        if (state->round < 1) state->round = 1;
    } else {
        fclose(f);
        show_message(state, "Load failed! Empty or corrupted save file.", 1);
        return;
    }

    int idx = 0;
    while (fgets(line, sizeof(line), f) && idx < MAX_COMBATANTS) {
        Combatant* c = &state->combatants[idx];

        /* Parse required fields - skip entire entry if any fail */
        char* token = strtok(line, "|");
        if (!token || !parse_int_safe(token, &c->id)) {
            show_message(state, "Load warning: Skipping malformed combatant entry (invalid ID).", 1);
            continue;
        }

        token = strtok(NULL, "|");
        if (!token) {
            show_message(state, "Load warning: Skipping malformed combatant entry (missing name).", 1);
            continue;
        }
        strncpy(c->name, token, NAME_LENGTH);
        c->name[NAME_LENGTH-1] = '\0';

        token = strtok(NULL, "|");
        int type_val;
        if (!token || !parse_int_safe(token, &type_val)) {
            show_message(state, "Load warning: Skipping malformed combatant entry (invalid type).", 1);
            continue;
        }
        c->type = (CombatantType)type_val;

        token = strtok(NULL, "|");
        if (!token || !parse_int_safe(token, &c->initiative)) {
            show_message(state, "Load warning: Skipping malformed combatant entry (invalid initiative).", 1);
            continue;
        }

        token = strtok(NULL, "|");
        if (!token || !parse_int_safe(token, &c->dex)) {
            show_message(state, "Load warning: Skipping malformed combatant entry (invalid dex).", 1);
            continue;
        }

        token = strtok(NULL, "|");
        if (!token || !parse_int_safe(token, &c->max_hp)) {
            show_message(state, "Load warning: Skipping malformed combatant entry (invalid max_hp).", 1);
            continue;
        }

        token = strtok(NULL, "|");
        if (!token || !parse_int_safe(token, &c->hp)) {
            show_message(state, "Load warning: Skipping malformed combatant entry (invalid hp).", 1);
            continue;
        }

        token = strtok(NULL, "|");
        int conditions_val;
        if (!token || !parse_int_safe(token, &conditions_val)) {
            show_message(state, "Load warning: Skipping malformed combatant entry (invalid conditions).", 1);
            continue;
        }
        c->conditions = (uint16_t)conditions_val;

        /* Backward compatibility: death save fields may be missing in old saves */
        token = strtok(NULL, "|");
        if (token) {
            if (!parse_int_safe(token, &c->death_save_successes)) {
                c->death_save_successes = 0;  /* Default on parse failure */
            }
        } else {
            c->death_save_successes = 0;
        }

        token = strtok(NULL, "|");
        if (token) {
            if (!parse_int_safe(token, &c->death_save_failures)) {
                c->death_save_failures = 0;  /* Default on parse failure */
            }
        } else {
            c->death_save_failures = 0;
        }

        token = strtok(NULL, "|");
        if (token) {
            if (!parse_int_safe(token, &c->is_stable)) {
                c->is_stable = 0;  /* Default on parse failure */
            }
        } else {
            c->is_stable = 0;
        }

        token = strtok(NULL, "|");
        if (token) {
            if (!parse_int_safe(token, &c->is_dead)) {
                c->is_dead = 0;  /* Default on parse failure */
            }
        } else {
            c->is_dead = 0;
        }

        for(int j=0; j<NUM_CONDITIONS; j++) {
            token = strtok(NULL, "|");
            if (!token) break;
            if (!parse_int_safe(token, &c->condition_duration[j])) {
                c->condition_duration[j] = 0;  /* Default on parse failure */
            }
        }

        idx++;
    }
    state->count = idx;

    if (fclose(f) != 0) {
        show_message(state, "Load warning: Error closing file.", 1);
    }

    /* Recalculate next_id to prevent collisions from corrupt/manually edited save files */
    int max_id = 0;
    for (int i = 0; i < state->count; i++) {
        if (state->combatants[i].id > max_id) {
            max_id = state->combatants[i].id;
        }
    }
    state->next_id = max_id + 1;
    if (state->next_id <= 0 || state->next_id == INT_MAX) {
        state->next_id = 1;  /* Fallback to 1 if overflow or invalid */
    }

    sort_combatants(state);
    state->message_queue_count = 0;

    log_action(state, "Game Loaded from save file. Round set to %d.", state->round);
    show_message(state, "Game Loaded.", 0);
}

/* --- Death Save Functions --- */

void reset_death_saves(Combatant* c) {
    c->death_save_successes = 0;
    c->death_save_failures = 0;
    c->is_stable = 0;
}

/**
 * Roll a death saving throw for a combatant.
 *
 * @param state Game state pointer
 * @param c Combatant pointer. If NULL, uses the currently selected combatant (state->selected_id).
 *          This allows convenient manual death saves via 'X' key without requiring selection.
 *
 * Rules implemented:
 * - Natural 20: Regain 1 HP immediately
 * - Natural 1: Two failures
 * - 10-19: Success (counts toward 3 successes = stable)
 * - 2-9: Failure (counts toward 3 failures = death)
 * - Only works for players at 0 HP who are not stable or dead
 */
void roll_death_save(GameState* state, Combatant* c) {
    /* Convenience: NULL uses selected combatant for manual 'X' key */
    if (!c) {
        int idx = get_index_by_id(state, state->selected_id);
        if (idx == -1) return;
        c = &state->combatants[idx];
    }

    if (c->type != TYPE_PLAYER) return;
    if (c->hp > 0 || c->is_stable || c->is_dead) return;

    int roll = (rand() % 20) + 1;

    if (roll == 20) {
        /* 5e rule: natural 20 = regain 1 HP immediately */
        c->hp = 1;
        c->conditions &= (uint16_t)~COND_UNCONSCIOUS;
        reset_death_saves(c);
        show_message(state, "NATURAL 20! Regained 1 HP!", 0);
        log_action(state, "%s rolled a NATURAL 20 on death save! Regained 1 HP.", c->name);
    } else if (roll == 1) {
        /* 5e rule: natural 1 = two failures */
        c->death_save_failures += 2;
        log_action(state, "%s rolled a NATURAL 1 on death save (2 failures). Total: %d failures.",
            c->name, c->death_save_failures);

        if (c->death_save_failures >= 3) {
            c->is_dead = 1;
            show_message(state, "DEATH! (3 failures)", 1);
            log_action(state, "%s has died (3 death save failures).", c->name);
        }
    } else if (roll >= 10) {
        c->death_save_successes++;
        log_action(state, "%s rolled %d on death save (SUCCESS). Total: %d successes.",
            c->name, roll, c->death_save_successes);

        if (c->death_save_successes >= 3) {
            c->is_stable = 1;
            show_message(state, "STABLE! (3 successes)", 0);
            log_action(state, "%s is now STABLE (3 death save successes).", c->name);
        }
    } else {
        c->death_save_failures++;
        log_action(state, "%s rolled %d on death save (FAILURE). Total: %d failures.",
            c->name, roll, c->death_save_failures);

        if (c->death_save_failures >= 3) {
            c->is_dead = 1;
            show_message(state, "DEATH! (3 failures)", 1);
            log_action(state, "%s has died (3 death save failures).", c->name);
        }
    }
}

void handle_damage_at_zero_hp(GameState* state, Combatant* c, int damage, int is_critical) {
    if (c->type != TYPE_PLAYER || c->hp > 0 || c->is_dead) return;

    /* 5e rule: damage at 0 HP = 1 failure, critical = 2 failures */
    if (is_critical) {
        c->death_save_failures += 2;
        log_action(state, "%s took %d CRITICAL damage at 0 HP (2 failures). Total: %d failures.",
            c->name, damage, c->death_save_failures);
    } else {
        c->death_save_failures++;
        log_action(state, "%s took %d damage at 0 HP (1 failure). Total: %d failures.",
            c->name, damage, c->death_save_failures);
    }

    /* Damage breaks stability */
    if (c->is_stable) {
        c->is_stable = 0;
        log_action(state, "%s is no longer stable due to damage.", c->name);
    }

    if (c->death_save_failures >= 3) {
        c->is_dead = 1;
        show_message(state, "DEATH! (3 failures from damage)", 1);
        log_action(state, "%s has died (3 death save failures).", c->name);
    }
}

void stabilize_combatant(GameState* state) {
    int idx = get_index_by_id(state, state->selected_id);
    if (idx == -1) return;

    Combatant* c = &state->combatants[idx];

    if (c->type != TYPE_PLAYER) {
        show_message(state, "Only players can be stabilized!", 1);
        return;
    }

    if (c->hp > 0) {
        show_message(state, "Combatant is not at 0 HP!", 1);
        return;
    }

    if (c->is_dead) {
        show_message(state, "Combatant is already dead!", 1);
        return;
    }

    if (c->is_stable) {
        show_message(state, "Combatant is already stable!", 1);
        return;
    }

    reset_death_saves(c);
    c->is_stable = 1;
    show_message(state, "Combatant stabilized!", 0);
    log_action(state, "%s has been stabilized (Spare the Dying/Medicine check/Healer's Kit).", c->name);
}

/* --- Helper Functions --- */

void move_selection(GameState* state, int direction) {
    int idx = get_index_by_id(state, state->selected_id);
    if (idx == -1) {
        if (state->count > 0) state->selected_id = state->combatants[0].id;
        return;
    }

    idx = (idx + direction + state->count) % state->count;
    state->selected_id = state->combatants[idx].id;
}

void sort_combatants(GameState* state) {
    if (state->count <= 1) return;
    qsort(state->combatants, (size_t)state->count, sizeof(Combatant), compare_combatants);
}

int compare_combatants(const void* a, const void* b) {
    const Combatant* ca = (const Combatant*)a;
    const Combatant* cb = (const Combatant*)b;

    if (cb->initiative != ca->initiative) {
        return cb->initiative - ca->initiative;
    }
    if (cb->dex != ca->dex) {
        return cb->dex - ca->dex;
    }
    return ca->id - cb->id;
}

int get_index_by_id(GameState* state, int id) {
    for (int i = 0; i < state->count; i++) {
        if (state->combatants[i].id == id) return i;
    }
    return -1;
}

/**
 * Safely parse an integer from a string.
 *
 * @param str Input string to parse (must not be NULL)
 * @param out Pointer to store parsed value (must not be NULL)
 * @return 1 on success, 0 on failure (invalid format, overflow, etc.)
 *
 * NOTE: Accepts optional leading/trailing whitespace.
 * Returns 0 for: NULL input, empty string, non-numeric characters,
 * overflow/underflow beyond INT_MIN/INT_MAX.
 */
int parse_int_safe(const char* str, int* out) {
    if (str == NULL || *str == '\0') return 0;

    char* endptr;
    errno = 0;  /* Reset errno before call */
    long val = strtol(str, &endptr, 10);

    /* Check for conversion errors */
    if (endptr == str) return 0;  /* No digits parsed */

    /* Skip trailing whitespace */
    while (*endptr == ' ' || *endptr == '\t') endptr++;

    /* Ensure entire string was consumed (except whitespace) */
    if (*endptr != '\0' && *endptr != '\n' && *endptr != '\r') return 0;

    /* Check for overflow/underflow */
    if (errno == ERANGE || val > INT_MAX || val < INT_MIN) return 0;

    *out = (int)val;
    return 1;
}

/**
 * Get condition name safely with bounds checking.
 *
 * @param index Condition index (0 to NUM_CONDITIONS-1)
 * @return Condition name string, or "Unknown" if index is invalid
 */
const char* get_condition_name(int index) {
    if (index >= 0 && index < NUM_CONDITIONS) {
        return condition_data[index].name;
    }
    return "Unknown";
}

/**
 * Get string input from user with proper cursor management.
 *
 * @param prompt Prompt string to display
 * @param buffer Buffer to store input (must be at least max_len bytes)
 * @param max_len Maximum length including null terminator
 * @return 1 on success (non-empty input), 0 on cancellation or empty input
 */
int get_input_string(const char* prompt, char* buffer, int max_len) {
    if (!buffer || max_len < 1) return 0;

    int rows, cols_unused;
    getmaxyx(stdscr, rows, cols_unused);
    (void)cols_unused;
    int input_y = rows - 2;

    attron(COLOR_PAIR(COLOR_HEADER));
    mvprintw(input_y, 0, "%s", prompt);
    clrtoeol();
    attroff(COLOR_PAIR(COLOR_HEADER));

    int prompt_len = (int)strlen(prompt);
    move(input_y, prompt_len);

    curs_set(1);
    refresh();

    int ret = 0;
    noecho();
    int ch = getch();

    if (ch != 27 && ch != '\n' && ch != '\r') {
        ungetch(ch);
        echo();
        if (getnstr(buffer, max_len - 1) != ERR && buffer[0] != '\0') {
            ret = 1;
        }
    }

    noecho();
    curs_set(0);

    move(input_y, 0);
    clrtoeol();
    refresh();

    return ret;
}

/**
 * Get integer input from user with validation and error feedback.
 *
 * @param state Game state for message display
 * @param prompt Prompt string to display
 * @param value Pointer to store parsed integer value
 * @param min_val Minimum allowed value (INT_MIN if no limit)
 * @param max_val Maximum allowed value (INT_MAX if no limit)
 * @return 1 on success, 0 on cancellation or invalid input
 */
int get_input_int(GameState* state, const char* prompt, int* value, int min_val, int max_val) {
    char buf[32];
    int attempts = 0;
    const int max_attempts = 3;

    while (attempts < max_attempts) {
        if (!get_input_string(prompt, buf, sizeof(buf))) {
            return 0; /* User cancelled */
        }

        if (parse_int_safe(buf, value)) {
            if (*value < min_val || *value > max_val) {
                char err_msg[128];
                snprintf(err_msg, sizeof(err_msg), "Value must be between %d and %d", min_val, max_val);
                show_message(state, err_msg, 1);
                attempts++;
                continue;
            }
            return 1;
        }

        show_message(state, "Invalid number! Please enter a valid integer.", 1);
        attempts++;
    }

    show_message(state, "Too many invalid attempts. Cancelled.", 1);
    return 0;
}

int get_input_char(const char* prompt, const char* allowed) {
    attron(COLOR_PAIR(COLOR_HEADER));
    mvprintw(LINES-2, 0, "%s", prompt);
    clrtoeol();
    attroff(COLOR_PAIR(COLOR_HEADER));
    while(1) {
        int ch = getch();
        if(ch == 27) return 0;
        if(strchr(allowed, tolower(ch))) return ch;
    }
}

int get_input_confirm(const char* prompt) {
    int ch = get_input_char(prompt, "ynYN");
    return (tolower(ch) == 'y');
}

void show_message(GameState* state, const char* msg, int is_error) {
    if (!state) return;

    clear_old_messages(state);

    /* Drop oldest message when queue is full (FIFO) */
    if (state->message_queue_count >= MAX_MESSAGE_QUEUE) {
        for (int i = 0; i < MAX_MESSAGE_QUEUE - 1; i++) {
            state->message_queue[i] = state->message_queue[i + 1];
        }
        state->message_queue_count = MAX_MESSAGE_QUEUE - 1;
    }

    MessageQueueEntry* entry = &state->message_queue[state->message_queue_count];
    strncpy(entry->text, msg, sizeof(entry->text) - 1);
    entry->text[sizeof(entry->text) - 1] = '\0';
    entry->is_error = is_error;
    entry->timestamp = time(NULL);
    state->message_queue_count++;
}

void clear_old_messages(GameState* state) {
    if (!state) return;

    time_t now = time(NULL);
    int write_idx = 0;

    for (int i = 0; i < state->message_queue_count; i++) {
        double elapsed = difftime(now, state->message_queue[i].timestamp);
        if (elapsed < MESSAGE_DISPLAY_DURATION_SECONDS) {
            if (write_idx != i) {
                state->message_queue[write_idx] = state->message_queue[i];
            }
            write_idx++;
        }
    }

    state->message_queue_count = write_idx;
}

/**
 * Draw message queue at the bottom of the screen.
 * Uses a reserved area to prevent visual artifacts from clearing/redrawing.
 */
void draw_message_queue(GameState* state) {
    if (!state || state->message_queue_count == 0) return;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    /* Reserve bottom area to prevent message overlap with content */
    int message_area_start = rows - MAX_MESSAGE_QUEUE - 1;
    if (message_area_start < 0) message_area_start = 0;

    /* Draw newest messages at bottom, older ones above */
    int y = rows - 1;
    for (int i = state->message_queue_count - 1; i >= 0 && y >= message_area_start; i--) {
        MessageQueueEntry* entry = &state->message_queue[i];
        int pair = entry->is_error ? COLOR_MSG_ERROR : COLOR_MSG_SUCCESS;

        int text_len = (int)strlen(entry->text);
        int x_start = cols / 2 - text_len / 2 - 1;
        if (x_start < 0) x_start = 0;
        if (x_start + text_len + 2 > cols) x_start = cols - text_len - 2;

        move(y, 0);
        clrtoeol();

        attron(COLOR_PAIR(pair) | A_BOLD);
        mvprintw(y, x_start, " %s ", entry->text);
        attroff(COLOR_PAIR(pair) | A_BOLD);
        y--;
    }

    while (y >= message_area_start && y >= 0) {
        move(y, 0);
        clrtoeol();
        y--;
    }
}
