# D&D Initiative Tracker

A terminal-based initiative tracker for Dungeons & Dragons 5th Edition combat encounters, built with C and ncurses.

## Features

- **Combatant Management**: Add, remove, and manage players and enemies
- **Initiative Tracking**: Automatic sorting by initiative and dexterity
- **HP Tracking**: Visual HP indicators with color coding (Good/Hurt/Critical/Unconscious/Dead)
- **Condition Management**: Apply and track 15 different conditions with optional durations
- **Turn Management**: Navigate through combat rounds with next/previous turn controls
- **Combat Logging**: Automatic logging of combat actions with export functionality
- **Undo System**: Undo last action (up to 10 states)
- **Save/Load**: Persist game state between sessions
- **Color-Coded UI**: Visual distinction between players and enemies

## Requirements

- GCC compiler
- ncurses library

### Installing ncurses

**Linux (Debian/Ubuntu):**
```bash
sudo apt-get install libncurses5-dev libncursesw5-dev
```

**Linux (Fedora/RHEL):**
```bash
sudo dnf install ncurses-devel
```

**macOS:**
```bash
brew install ncurses
```

**Windows (WSL):**
```bash
sudo apt-get install libncurses5-dev libncursesw5-dev
```

## Compilation

Using Makefile (recommended):
```bash
make
```

Or manually:
```bash
gcc -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror -std=c11 initiative.c -lncurses -o initiative
```

Or with standard warnings:
```bash
gcc initiative.c -lncurses -o initiative
```

### Makefile Targets

- `make` or `make all` - Build the executable
- `make clean` - Remove compiled binaries
- `make install` - Install to `/usr/local/bin` (optional)
- `make uninstall` - Remove from `/usr/local/bin`

## Usage

```bash
./initiative
```

### Controls

- **A** - Add combatant
- **D** - Delete selected combatant
- **H** - Edit HP (heal/damage)
- **C** - Toggle conditions
- **N** - Next turn
- **P** - Previous turn
- **R** - Reroll initiative
- **S** - Save game state
- **L** - Load game state
- **E** - Export combat log
- **Z** - Undo last action
- **↑/↓** - Navigate selection
- **Q** - Quit

## Game Rules

- **Players**: Go unconscious at 0 HP (not dead)
- **Enemies**: Die at 0 HP
- **Initiative**: Sorted by initiative roll, then dexterity modifier
- **Conditions**: Can be applied with optional durations (in rounds)

## File Locations

- **Save file**: `~/.dnd_tracker_save.txt`
- **Log export**: `~/combat_log_export.txt`

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.