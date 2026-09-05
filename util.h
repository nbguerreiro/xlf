#ifndef UTIL_H
#define UTIL_H

#include <sys/types.h>

char *get_absolute_path(const char *path);
char *get_display_path(const char *path);
void format_file_info(const char *path, const char *name, int is_dir,
                      char *buf, int buf_size);
int tool_is_available(const char *tool_name);
void check_tool_availability(void);

#endif
