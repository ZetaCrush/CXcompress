#define THREAD_COUNT 4
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

#include "utils.h"
#include "m-thread.h"

#define MAX_WORD_LEN 128

void process_chunk(const char *s, size_t start, size_t end, char C0, char C1, char **new_words, size_t *new_words_count, size_t *new_words_capacity) {
    char word[MAX_WORD_LEN];
    size_t word_len = 0;

    for (size_t i = start; i < end; ++i) {
        char c = s[i];

        if (c == ' ' || c == '\0') {
            word[word_len] = '\0';
            int space_n = 0;
            while (s[i + 1] == ' ') {
                space_n++;
                i++;
            }

            const char *sym = get_symbol_by_word(word);
            if (sym) {
                append_to_string_array(&new_words, new_words_count, new_words_capacity, strdup(sym), space_n);
            } else if (word_len > 1 && is_sep(word[word_len - 1])) {
                char base[MAX_WORD_LEN];
                strncpy(base, word, word_len - 1);
                base[word_len - 1] = '\0';
                const char *sym2 = get_symbol_by_word(base);
                if (sym2) {
                    char *combined = malloc(strlen(sym2) + 2);
                    sprintf(combined, "%s%c", sym2, word[word_len - 1]);
                    append_to_string_array(&new_words, new_words_count, new_words_capacity, combined, space_n);
                } else {
                    const char *gfound = get_word_by_symbol(base);
                    if (gfound) {
                        char *marked = malloc(strlen(word) + 2);
                        sprintf(marked, "%s%c", word, C0);
                        append_to_string_array(&new_words, new_words_count, new_words_capacity, marked, space_n);
                    } else {
                        append_to_string_array(&new_words, new_words_count, new_words_capacity, strdup(word), space_n);
                    }
                }
            } else {
                const char *gfound = get_word_by_symbol(word);
                if (gfound) {
                    char *marked = malloc(strlen(word) + 2);
                    sprintf(marked, "%c%s", C0, word);
                    append_to_string_array(&new_words, new_words_count, new_words_capacity, marked, space_n);
                } else {
                    append_to_string_array(&new_words, new_words_count, new_words_capacity, strdup(word), space_n);
                }
            }

            word_len = 0;
            if (c == '\0') {
                break;
            }
        } else {
            word[word_len++] = c;
        }
    }
}

// Main function to process the input string in parallel and maintain order.
char* process_words_parallel(const char *s, char C0, char C1, size_t thread_count) {
    size_t length = strlen(s);
    size_t chunk_size = length / thread_count;
    
    // Create an array of buffers (one for each thread)
    char ***new_words_buffers = malloc(thread_count * sizeof(char**));
    size_t *new_words_counts = malloc(thread_count * sizeof(size_t));
    size_t *new_words_capacities = malloc(thread_count * sizeof(size_t));
    for (size_t i = 0; i < thread_count; i++) {
        new_words_buffers[i] = malloc(1000 * sizeof(char*)); // Initialize buffers for each thread
        new_words_counts[i] = 0;
        new_words_capacities[i] = 1000;
    }

    // Parallel processing: Each thread processes its own chunk
    #pragma omp parallel num_threads(thread_count)
    {
        int thread_id = omp_get_thread_num();
        size_t start = thread_id * chunk_size;
        size_t end = (thread_id == thread_count - 1) ? length : (start + chunk_size);

        // Process the chunk for this thread
        process_chunk(s, start, end, C0, C1, new_words_buffers[thread_id], &new_words_counts[thread_id], &new_words_capacities[thread_id]);
    }

    // After processing chunks in parallel, we need to combine the results while maintaining order
    size_t new_words_count = 0;
    size_t new_words_capacity = 1000;
    char **new_words = malloc(new_words_capacity * sizeof(char*));

    // Append results from each thread in order
    for (size_t i = 0; i < thread_count; i++) {
        for (size_t j = 0; j < new_words_counts[i]; j++) {
            append_to_string_array(&new_words, &new_words_count, &new_words_capacity, new_words_buffers[i][j], 0);
        }
        free(new_words_buffers[i]);
    }

    free(new_words_buffers);
    free(new_words_counts);
    free(new_words_capacities);

    // Build the final result string
    size_t result_len = 3 + strlen("some symbols"); // Starting length for result
    for (size_t i = 0; i < new_words_count; ++i) {
        result_len += strlen(new_words[i]) + 1;
    }

    char *result = malloc(result_len + 1);
    if (!result) return NULL;

    char *p = result;
    *p++ = C0;
    *p++ = C1;

    for (size_t i = 0; i < new_words_count; ++i) {
        size_t len = strlen(new_words[i]);
        memcpy(p, new_words[i], len);
        p += len;
        if (i < new_words_count - 1) *p++ = ' ';
        free(new_words[i]);
    }
    *p = '\0';

    free(new_words);
    return result;
}