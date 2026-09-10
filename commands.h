#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>

/*
 * External commands added to fm's unified ':' command menu.
 *
 * name    - command name shown in dmenu
 * command - executable/script name, resolved through PATH
 * key     - optional shortcut, e.g. "C-i"; use NULL for none
 *
 * The selected file's path is passed as argv[1].
 *
 * Example:
 *     {"com", "com.sh", "C-i"},
 */
typedef struct {
    const char *name;
    const char *command;
    const char *key;
} ExternalCommand;

static const ExternalCommand external_commands[] = {
    {"notify", "notify-send", "C-n"},
    {NULL, NULL, NULL}
};

enum {
    EXTERNAL_COMMAND_COUNT =
        (int)(sizeof(external_commands) / sizeof(external_commands[0]) - 1)
};

#endif
