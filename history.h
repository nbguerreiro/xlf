#ifndef HISTORY_H
#define HISTORY_H

/*
 * Persistent command history for fm.
 * History is stored under $XDG_STATE_HOME/fm when XDG_STATE_HOME is set,
 * otherwise under ~/.local/state/fm.
 */
int history_add(const char *command);
char *history_menu(void);

#endif
