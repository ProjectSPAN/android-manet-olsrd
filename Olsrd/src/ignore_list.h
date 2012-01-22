#ifndef IGNORE_LIST
#define IGNORE_LIST

#include <stdbool.h>

#include "olsr_types.h"

#define IGNORELISTSIZE 128

void olsrd_parse_ignore_list(const char *filename);

bool is_on_ignore_list(union olsr_ip_addr addr);

#endif
