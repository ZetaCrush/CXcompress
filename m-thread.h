#define THREAD_COUNT 4
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_WORD_LEN 128

void process_chunk(const char *s, size_t start, size_t end, char C0, char C1, char **new_words, size_t *new_words_count, size_t *new_words_capacity);
char* process_words_parallel(const char *s, char C0, char C1, size_t thread_count);