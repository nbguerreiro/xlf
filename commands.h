#ifndef COMMANDS_H
#define COMMANDS_H

/*
 * External commands shown by the ':' command menu.
 *
 * name    - name shown in dmenu
 * command - executable/script name, resolved through PATH
 * key     - optional shortcut, e.g. "C-i"; use NULL for none
 *
 * The selected file's absolute path is passed as argv[1].
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
    /* {"com", "com.sh", "C-i"}, */
    {NULL, NULL, NULL}
};

#define EXTERNAL_COMMAND_COUNT \
    (sizeof(external_commands) / sizeof(external_commands[0]) - 1)

#endif
