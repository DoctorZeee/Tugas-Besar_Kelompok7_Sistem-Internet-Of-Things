#ifndef COMMAND_UTIL_H
#define COMMAND_UTIL_H

#include <Arduino.h>
#include <string.h>
#include <ctype.h>

// Rapikan perintah: buang spasi di ujung, jadikan huruf besar (in-place, tanpa alokasi memori).
inline void normalizeCommand(char* s) {
  size_t begin = 0;
  while (s[begin] && isspace((unsigned char)s[begin])) ++begin;
  size_t end = strlen(s);
  while (end > begin && isspace((unsigned char)s[end - 1])) s[--end] = '\0';
  if (begin > 0) memmove(s, s + begin, end - begin + 1);
  for (size_t i = 0; s[i]; ++i) s[i] = (char)toupper((unsigned char)s[i]);
}

#endif
