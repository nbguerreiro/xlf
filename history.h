#ifndef HISTORY_H
#define HISTORY_H

/*
 * Persistent command history for xlf.
 * History is stored under $XDG_STATE_HOME/xlf when XDG_STATE_HOME is set,
 * otherwise under ~/.local/state/xlf. A pre-existing fm/history file is
 * migrated to the xlf location on first use.
 */
int history_add(const char *command);
char *history_menu(void);

#endif
