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
 
 #define MAX_COMBATANTS 50
 #define NAME_LENGTH 32
 #define NUM_CONDITIONS 15
 #define SAVE_FILE_NAME ".dnd_tracker_save.txt"
 #define LOG_EXPORT_FILE_NAME "combat_log_export.txt"
 
 /* Conditions as bit flags */
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
 
 const char* condition_names[NUM_CONDITIONS] = {
     "Blinded", "Charmed", "Deafened", "Frightened", "Grappled",
     "Incapacitated", "Poisoned", "Prone", "Restrained", "Stunned",
     "Invisible", "Paralyzed", "Petrified", "Unconscious", "Exhaustion"
 };
 
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
 
 /* New Feature Prototypes */
 void log_action(GameState* state, const char* format, ...);
 void export_log(GameState* state);
 void save_undo_state(GameState* state);
 void undo_last_action(GameState* state);
 
 /* Helper Prototypes */
 int get_input_string(const char* prompt, char* buffer, int max_len);
 int get_input_int(const char* prompt, int* value);
 int get_input_char(const char* prompt, const char* allowed);
 int get_input_confirm(const char* prompt);
 void show_message(const char* msg, int is_error);
 int get_index_by_id(GameState* state, int id);
 int parse_int_safe(const char* str, int* out);
 
 int main(void) {
     GameState state = {0};
     state.round = 1;
     state.next_id = 1;
     state.current_turn_id = -1;
     state.selected_id = -1;
     init_log(&state); /* Initialize dynamic log array */
 
     initscr();
     cbreak();
     noecho();
     keypad(stdscr, TRUE);
     curs_set(0);
 
     if (has_colors()) {
         start_color();
         init_colors();
     }
 
     int running = 1;
     while (running) {
         draw_ui(&state);
 
         int ch = getch();
         switch (tolower(ch)) {
             case 'q': running = 0; break;
             case 'a': save_undo_state(&state); add_combatant(&state); break;
             case 'd': if (state.count > 0) { save_undo_state(&state); remove_combatant(&state); } break; 
             case 'h': if (state.count > 0) { save_undo_state(&state); edit_hp(&state); } break;
             case 'r': if (state.count > 0) { save_undo_state(&state); reroll_initiative(&state); } break;
             case 'c': if (state.count > 0) toggle_condition(&state); break; 
             case 'n': if (state.count > 0) { save_undo_state(&state); next_turn(&state); } break;
             case 'p': if (state.count > 0) { save_undo_state(&state); prev_turn(&state); } break;
             case 's': save_state(&state); break;
             case 'l': load_state(&state); break; 
             case 'e': if (state.log_count > 0) export_log(&state); break; /* Export Log */
             case 'z': undo_last_action(&state); break; /* Undo */
             case KEY_UP:
                 if (state.count > 0) move_selection(&state, -1);
                 break;
             case KEY_DOWN:
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
     init_pair(COLOR_MENU_NORM, COLOR_WHITE, COLOR_BLACK);
     init_pair(COLOR_MSG_SUCCESS, COLOR_BLACK, COLOR_GREEN);
     init_pair(COLOR_MSG_ERROR, COLOR_WHITE, COLOR_RED);
 }
 
 /* --- Logging Functions --- */
 
void init_log(GameState* state) {
    state->log_capacity = 10;
    state->combat_log = (CombatLogEntry*)calloc((size_t)state->log_capacity, sizeof(CombatLogEntry));
     if (!state->combat_log) {
         /* If initial allocation fails, log is disabled */
         state->log_capacity = 0;
         state->log_count = 0;
     }
 }
 
 void cleanup_log(GameState* state) {
     /* Defensive null pointer check before freeing memory */
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
         
         /* Defensive check: If realloc fails, return without logging or crashing */
         if (!new_log) return; 
         
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
 
 void export_log(GameState* state) {
     char path[256];
     const char* home = getenv("HOME");
     if (home) snprintf(path, sizeof(path), "%s/%s", home, LOG_EXPORT_FILE_NAME);
     else snprintf(path, sizeof(path), "%s", LOG_EXPORT_FILE_NAME);
 
     FILE* f = fopen(path, "a"); /* Append mode for session notes */
     if (!f) {
         show_message("Log export failed!", 1);
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
 
     /* Clear log after successful export */
     state->log_count = 0;
     show_message("Log Exported and Cleared!", 0);
 }
 
 /* --- Undo Functions --- */
 
 void save_undo_state(GameState* state) {
     /* If the stack is full, shift all elements up to make space for the new one */
     if (state->undo_count >= MAX_UNDO_STACK) {
         memmove(&state->undo_stack[0], &state->undo_stack[1], (MAX_UNDO_STACK - 1) * sizeof(UndoState));
         state->undo_count = MAX_UNDO_STACK - 1;
     }
 
    UndoState* current_undo = &state->undo_stack[state->undo_count];
    
    /* Copy only the essential, mutable parts of GameState */
    memcpy(current_undo->combatants, state->combatants, (size_t)state->count * sizeof(Combatant));
     current_undo->count = state->count;
     current_undo->current_turn_id = state->current_turn_id;
     current_undo->selected_id = state->selected_id;
     current_undo->round = state->round;
 
     state->undo_count++;
 }
 
 void undo_last_action(GameState* state) {
     if (state->undo_count == 0) {
         show_message("Nothing to undo!", 1);
         return;
     }
 
    state->undo_count--;
    UndoState* prev_state = &state->undo_stack[state->undo_count];

    /* Restore the previous state */
    state->count = prev_state->count;
    memcpy(state->combatants, prev_state->combatants, (size_t)state->count * sizeof(Combatant));
     state->current_turn_id = prev_state->current_turn_id;
     state->selected_id = prev_state->selected_id;
     state->round = prev_state->round;
     
     /* Log the undo event */
     log_action(state, "Action UNDONE. Reverted to start of Round %d.", state->round);
     
     show_message("Undo successful!", 0);
 }
 
 /* --- TUI/Core Functions --- */
 
 void draw_ui(GameState* state) {
     int rows, cols;
     getmaxyx(stdscr, rows, cols);
     clear();
 
     /* Header */
     attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
     mvhline(0, 0, ' ', cols);
     mvprintw(0, 1, "D&D INITIATIVE TRACKER | Round: %d | Keys: A(dd) D(el) H(eal) C(ond) N(ext) R(eroll) E(xport) Z(undo) S(ave) L(oad)", state->round);
     attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
 
     /* Split Screen Calculation */
     int split_y = rows / 2;
     /* list_height is adjusted to account for the new header line (1 row vs 2 rows previously) */
     int list_height = split_y - 3; 
 
     /* Top: Players - inline with separator */
     mvhline(1, 0, ACS_HLINE, cols); /* Draw the separating line */
     attron(A_BOLD);
     mvprintw(1, 2, "[ PLAYERS ]"); /* Draw text over the line */
     attroff(A_BOLD);
     draw_filtered_list(state, 2, 0, cols, list_height, TYPE_PLAYER);
 
     /* Separator (Middle Line) */
     attron(COLOR_PAIR(COLOR_SEPARATOR));
     mvhline(split_y, 0, ACS_HLINE, cols);
     attroff(COLOR_PAIR(COLOR_SEPARATOR));
 
     /* Bottom: Enemies - already inline */
     attron(A_BOLD);
     mvprintw(split_y, 2, "[ ENEMIES ]");
     attroff(A_BOLD);
 
     draw_filtered_list(state, split_y + 1, 0, cols, rows - split_y - 1, TYPE_ENEMY);
 
     refresh();
 }
 
 void draw_filtered_list(GameState* state, int start_y, int start_x, int width, int height, CombatantType type) {
     if (state->count == 0) return;
 
     /* Column Headers */
     attron(A_UNDERLINE);
     mvprintw(start_y, start_x + 2, "%-20s %4s %4s %8s %s", "Name", "Init", "Dex", "HP", "Conditions");
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
 
         /* HP Visual Logic*/
         int hp_color = COLOR_HP_GOOD;
         
         if (c->hp <= 0) {
             /* 5e Rule: Enemies die, Players go Unconscious */
             if (c->type == TYPE_ENEMY) hp_color = COLOR_DEAD;
             else hp_color = COLOR_HP_UNCONSCIOUS;
         }
         else if (c->hp <= c->max_hp / 4) hp_color = COLOR_HP_CRITICAL;
         else if (c->hp <= c->max_hp / 2) hp_color = COLOR_HP_HURT;
 
         attron(COLOR_PAIR(hp_color));
         if (c->hp <= 0 && c->type == TYPE_PLAYER) {
             mvprintw(y, start_x + 33, "%s", " DOWN  ");
         } else if (c->hp <= 0 && c->type == TYPE_ENEMY) {
             mvprintw(y, start_x + 33, "%s", " DEAD  ");
         } else {
             mvprintw(y, start_x + 33, "%3d/%3d", c->hp, c->max_hp);
         }
         attroff(COLOR_PAIR(hp_color));
 
         /* Conditions */
         char cond_str[128] = "";
         for (int j = 0; j < NUM_CONDITIONS; j++) {
             if (c->conditions & (1 << j)) {
                 char temp[32];
                 if (c->condition_duration[j] > 0)
                     snprintf(temp, sizeof(temp), "%s(%d) ", condition_names[j], c->condition_duration[j]);
                 else
                     snprintf(temp, sizeof(temp), "%s ", condition_names[j]);
                 strcat(cond_str, temp);
             }
         }
         int remaining_w = width - 45;
         if (remaining_w > 0)
             mvprintw(y, start_x + 43, "%.*s", remaining_w, cond_str);
 
         y++;
     }
 
     /* Scroll Indicator */
     if (type_count > list_display_h && scroll_offset + list_display_h < type_count) {
         int indicator_y = start_y + height - 1;
         attron(A_BOLD);
         mvprintw(indicator_y, start_x + 2, "(%d more \u2193)", type_count - (scroll_offset + list_display_h));
         attroff(A_BOLD);
     }
 }
 
 void toggle_condition(GameState* state) {
     if (state->count == 0) return;
     int idx = get_index_by_id(state, state->selected_id);
     if (idx == -1) return;
 
     /* 1. Save state once upon entering the menu. All changes here are one undo step. */
     save_undo_state(state);
 
     Combatant* c = &state->combatants[idx];
     int cursor = 0;
     int viewing = 1;
 
     while (viewing) {
         clear();
         attron(A_BOLD | A_UNDERLINE);
         mvprintw(1, 2, "Conditions for: %s", c->name);
         attroff(A_BOLD | A_UNDERLINE);
         mvprintw(2, 2, "UP/DOWN: Select | SPACE/ENTER: Toggle | 'd': Duration | 'q': Done");
         mvhline(3, 0, ACS_HLINE, COLS);
 
         for (int i = 0; i < NUM_CONDITIONS; i++) {
             int is_active = c->conditions & (1 << i);
             int is_selected = (i == cursor);
 
             int pair = is_selected ? COLOR_MENU_SEL : COLOR_MENU_NORM;
             attron(COLOR_PAIR(pair));
             if (is_selected) attron(A_BOLD);
 
             mvhline(4 + i, 0, ' ', COLS);
             mvprintw(4 + i, 4, "[%c] %-15s", is_active ? 'X' : ' ', condition_names[i]);
 
             if (is_active && c->condition_duration[i] > 0) {
                 printw(" (Duration: %d)", c->condition_duration[i]);
             }
 
             if (is_selected) attroff(A_BOLD);
             attroff(COLOR_PAIR(pair));
         }
 
         refresh();
 
         int ch = getch();
         switch (ch) {
             case KEY_UP:
                 cursor--; if (cursor < 0) cursor = NUM_CONDITIONS - 1; break;
             case KEY_DOWN:
                 cursor++; if (cursor >= NUM_CONDITIONS) cursor = 0; break;
             case ' ':
             case '\n':
             case '\r':
                 {
                     int was_active = c->conditions & (1 << cursor);
                     c->conditions ^= (1 << cursor);
                     
                     if (!was_active && (c->conditions & (1 << cursor))) {
                         log_action(state, "%s: %s applied.", c->name, condition_names[cursor]);
                     } else if (was_active && !(c->conditions & (1 << cursor))) {
                         c->condition_duration[cursor] = 0;
                         log_action(state, "%s: %s removed.", c->name, condition_names[cursor]);
                     }
                 }
                 break;
             case 'd':
             case 'D':
                 if (c->conditions & (1 << cursor)) {
                     int dur;
                     if (get_input_int("Duration (rounds): ", &dur)) {
                          if (dur >= 0) {
                             c->condition_duration[cursor] = dur;
                             log_action(state, "%s: %s duration set to %d.", c->name, condition_names[cursor], dur);
                          }
                     }
                 } else {
                     show_message("Enable condition first!", 1);
                 }
                 break;
             case 'q':
             case 'Q':
             case 27:
                 viewing = 0; break;
         }
     }
 }
 
 
 void add_combatant(GameState* state) {
     if (state->count >= MAX_COMBATANTS) {
         show_message("List full!", 1);
         return;
     }
 
     Combatant c = {0};
     int type_char = get_input_char("Type? (P)layer / (E)nemy: ", "pePE");
     if (type_char == 0) return; 
 
     c.type = (tolower(type_char) == 'p') ? TYPE_PLAYER : TYPE_ENEMY;
 
     if (!get_input_string("Name: ", c.name, NAME_LENGTH)) return;
     if (!get_input_int("Initiative: ", &c.initiative)) return;
     if (!get_input_int("Dexterity (Tiebreaker): ", &c.dex)) return;
     
     int max_hp;
     if (!get_input_int("Max HP: ", &max_hp)) return;
     c.max_hp = max_hp;
     c.hp = c.max_hp;
 
     if (state->next_id == INT_MAX) {
         state->next_id = 1;
     }
     c.id = state->next_id++; 
     
     state->combatants[state->count++] = c;
     state->selected_id = c.id;
     if (state->count == 1) state->current_turn_id = c.id;
 
     sort_combatants(state);
     log_action(state, "Added %s: Init %d, HP %d.", c.name, c.initiative, c.max_hp);
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
     
     /* Log the action before deletion */
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
 
 void edit_hp(GameState* state) {
     int idx = get_index_by_id(state, state->selected_id);
     if (idx == -1) return;
     
     Combatant* c = &state->combatants[idx];
     char prompt[64];
     snprintf(prompt, 64, "%s (%d/%d) Change (+/-): ", c->name, c->hp, c->max_hp);
     
     int change;
     if (get_input_int(prompt, &change)) {
         
         int old_hp = c->hp;
         
         c->hp += change;
         if (c->hp > c->max_hp) c->hp = c->max_hp;
         if (c->hp < 0) c->hp = 0;
         
         if (change > 0) {
             log_action(state, "%s healed %d HP (%d/%d).", c->name, change, c->hp, c->max_hp);
         } else if (change < 0) {
             log_action(state, "%s took %d damage (%d/%d).", c->name, -change, c->hp, c->max_hp);
         }
 
         /* 5e Rule: Player Unconscious on 0 HP */
         if (c->type == TYPE_PLAYER) {
             if (c->hp == 0 && old_hp > 0) {
                 if (!(c->conditions & COND_UNCONSCIOUS)) {
                     c->conditions |= COND_UNCONSCIOUS;
                     show_message("Player is DOWN! (Unconscious applied)", 1);
                     log_action(state, "%s is UNCONSCIOUS.", c->name);
                 }
            } else if (c->hp > 0 && old_hp <= 0) {
                if (c->conditions & COND_UNCONSCIOUS) {
                    c->conditions &= (uint16_t)~COND_UNCONSCIOUS;
                    show_message("Player is UP! (Unconscious removed)", 0);
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
     if (get_input_int("New Init: ", &val)) {
         int old_init = state->combatants[idx].initiative;
         state->combatants[idx].initiative = val;
         sort_combatants(state);
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
     log_action(state, "%s's turn.", state->combatants[idx].name);
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
                    log_action(state, "%s: %s duration ended.", state->combatants[i].name, condition_names[j]);
                }
             }
         }
     }
 }
 
 void save_state(GameState* state) {
     char path[256];
     const char* home = getenv("HOME");
     if (home) snprintf(path, sizeof(path), "%s/%s", home, SAVE_FILE_NAME);
     else snprintf(path, sizeof(path), "%s", SAVE_FILE_NAME);
 
     FILE* f = fopen(path, "w");
     if (!f) {
         show_message("Save failed!", 1);
         return;
     }
 
     fprintf(f, "%d|%d|%d|%d|%d\n", 
         state->round, state->next_id, state->count, state->current_turn_id, state->selected_id);
 
     for (int i = 0; i < state->count; i++) {
         Combatant* c = &state->combatants[i];
         fprintf(f, "%d|%s|%d|%d|%d|%d|%d|%d",
             c->id, c->name, c->type, c->initiative, c->dex, c->max_hp, c->hp, c->conditions);
         
         for(int j=0; j<NUM_CONDITIONS; j++) {
             fprintf(f, "|%d", c->condition_duration[j]);
         }
         fprintf(f, "\n");
     }
 
     fclose(f);
     show_message("Game Saved.", 0);
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
         show_message("No save file found.", 1);
         return;
     }
 
     /* Wipe log and undo stack on successful load */
     state->log_count = 0; 
     state->undo_count = 0;
     
     memset(state->combatants, 0, sizeof(state->combatants));
     
     char line[1024];
     if (fgets(line, sizeof(line), f)) {
         sscanf(line, "%d|%d|%d|%d|%d", 
             &state->round, &state->next_id, &state->count, &state->current_turn_id, &state->selected_id);
     }
 
    int idx = 0;
    while (fgets(line, sizeof(line), f) && idx < MAX_COMBATANTS) {
        Combatant* c = &state->combatants[idx];
        
        char* token = strtok(line, "|");
        if (!token) break;
        c->id = atoi(token);

        token = strtok(NULL, "|");
        if (!token) break;
        strncpy(c->name, token, NAME_LENGTH);
        c->name[NAME_LENGTH-1] = '\0';

        token = strtok(NULL, "|");
        if (!token) break;
        c->type = atoi(token);

        token = strtok(NULL, "|");
        if (!token) break;
        c->initiative = atoi(token);

        token = strtok(NULL, "|");
        if (!token) break;
        c->dex = atoi(token);

        token = strtok(NULL, "|");
        if (!token) break;
        c->max_hp = atoi(token);

        token = strtok(NULL, "|");
        if (!token) break;
        c->hp = atoi(token);

        token = strtok(NULL, "|");
        if (!token) break;
        c->conditions = (uint16_t)atoi(token);
 
         for(int j=0; j<NUM_CONDITIONS; j++) {
             token = strtok(NULL, "|");
             if (!token) break;
             c->condition_duration[j] = atoi(token);
        }
        idx++;
    }
    state->count = idx;
    fclose(f);
    
    /* Sort combatants by initiative after loading */
    sort_combatants(state);
    
    log_action(state, "Game Loaded from save file. Round set to %d.", state->round);
    show_message("Game Loaded.", 0);
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
 
 int parse_int_safe(const char* str, int* out) {
     if (str == NULL || *str == '\0') return 0;
     char* endptr;
     long val = strtol(str, &endptr, 10);
     if (endptr == str) return 0; 
     if (*endptr != '\0' && *endptr != '\n') return 0; 
     if (val > INT_MAX || val < INT_MIN) return 0;
     *out = (int)val;
     return 1;
 }
 
 int get_input_string(const char* prompt, char* buffer, int max_len) {
     attron(COLOR_PAIR(COLOR_HEADER));
     mvprintw(LINES-2, 0, "%s", prompt);
     clrtoeol();
     attroff(COLOR_PAIR(COLOR_HEADER));
     echo(); curs_set(1);
     int ret = (getnstr(buffer, max_len - 1) != ERR && buffer[0] != '\0');
     noecho(); curs_set(0);
     return ret;
 }
 
 int get_input_int(const char* prompt, int* value) {
     char buf[32];
     if(get_input_string(prompt, buf, 32)) {
         if (parse_int_safe(buf, value)) {
             return 1;
         }
         show_message("Invalid Number!", 1);
     }
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
 
void show_message(const char* msg, int is_error) {
    int pair = is_error ? COLOR_MSG_ERROR : COLOR_MSG_SUCCESS;
    attron(COLOR_PAIR(pair) | A_BOLD);
    mvprintw(LINES/2, COLS/2 - (int)(strlen(msg)/2), " %s ", msg);
    attroff(COLOR_PAIR(pair) | A_BOLD);
    refresh();
    napms(1000); 
}