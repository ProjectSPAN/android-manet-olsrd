#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "ignore_list.h"
#include "ipcalc.h"

struct in_addr ignore_list[IGNORELISTSIZE];
int ignore_list_size = 0;

void
olsrd_parse_ignore_list(const char *filename)
{
  FILE *ifp;
  char token[256];
  struct in_addr addr;

  fprintf(stdout, "Ignore list file: %s\n", filename); // DEBUG
  ifp = fopen(filename, "r");

  if (ifp == NULL) {
    fprintf(stdout, "Can't open input ignore list.\n"); // not an error
    return;
  }

  while (fgets(token, sizeof(token), ifp) != NULL) {
    // strip newline
    if (token[strlen(token)-1] == '\n') {
      token[strlen(token)-1] = '\0';
    }
    // parse
    if (token[0] != '#') { // comment
      if (inet_pton(AF_INET, token, &addr) != 1 ) {
        fprintf(stderr, "Failed converting IP address %s\n", token);
      } else {
        fprintf(stdout, "Ignore list entry: %s\n", token);
        ignore_list[ignore_list_size++] = addr;
      }
    }
  }
}

bool
is_on_ignore_list(union olsr_ip_addr addr)
{
  int i;
  char tmp1[64];
  char tmp2[64];
  inet_ntop(AF_INET, &addr.v4.s_addr, tmp1, sizeof(tmp1));
  for (i = 0; i < ignore_list_size; i++) {
    inet_ntop(AF_INET, &ignore_list[i].s_addr, tmp2, sizeof(tmp2));
    fprintf(stdout, "Comparing %s and %s\n", tmp1, tmp2); // DEBUG
    if (ip4equal(&addr.v4, &ignore_list[i])) {
      return true;
    }
  }
  return false;
}
