#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include "uthash.h"

char* read_file_to_string(const char* filename);
bool is_sep(char c);
char **split(char *s, char sep, size_t *out_count);
char **split_lines(char *input, size_t *out_count);
// void free_split(char** tokens, int count);
void find_unused_chars(const char *text, char *C0, char *C1);

#endif
