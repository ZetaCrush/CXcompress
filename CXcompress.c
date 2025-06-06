#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <omp.h>
#include "uthash.h"

#define MAX_LINE 1024
#define INITIAL_DICT_CAPACITY 1024 // Starting capacity for dictionary entries

// Global lookup tables for fast symbol/word checks.
// These are intentionally global to avoid passing them around and to
// leverage their fixed-size nature for cache efficiency.
// Initialized to all false/NULL/0 by default for static storage duration.
bool symbol_lookup[256][256][256];
char* word_lookup[256][256][256];
unsigned char word_lookup_len[256][256][256];

typedef struct {
    char* word;
    char* symbol;
} DictEntry;

typedef struct {
    char* key;
    char* value;
    size_t value_len;
    UT_hash_handle hh;
} HashEntry;

// Helper function to check if character is a delimiter
static inline bool is_delimiter(char c) {
    return (c == ' ' || c == 0 || c == ',' || c == '.' ||
            c == '?' || c == '!' || c == '\n' || c == '\r');
}

// Tokenizes the input buffer into spans of words or delimiters.
TokenSpan* tokenize(const char* input, size_t len, size_t* token_count_out) {
    size_t capacity = 1024;
    TokenSpan* spans = malloc(sizeof(TokenSpan) * capacity);
    if (!spans) {
        perror("Memory allocation failed for tokenization spans");
        exit(EXIT_FAILURE);
    }

    size_t i = 0, count = 0;

    while (i < len) {
        if (count >= capacity) {
            capacity *= 2;
            TokenSpan* new_spans = realloc(spans, sizeof(TokenSpan) * capacity);
            if (!new_spans) {
                perror("Memory reallocation failed for tokenization spans");
                free(spans); // Free previously allocated memory
                exit(EXIT_FAILURE);
            }
            spans = new_spans;
        }

        if (is_delimiter(input[i])) {
            spans[count++] = (TokenSpan){ .start = i, .len = 1, .is_space = true };
            i++;
        } else {
            size_t j = i;
            while (j < len && !is_delimiter(input[j])) {
                j++;
            }
            spans[count++] = (TokenSpan){ .start = i, .len = j - i, .is_space = false };
            i = j;
        }
    }

    *token_count_out = count;
    return spans;
}

// Checks if a word is a symbol using the fast 3-char lookup table.
bool is_symbol_fast(const char* word, size_t len) {
    if (len == 0 || len > 3) return false;
    unsigned char a = word[0];
    unsigned char b = (len > 1) ? word[1] : 0;
    unsigned char c = (len > 2) ? word[2] : 0;
    return symbol_lookup[a][b][c];
}

// Loads the dictionary from two files and populates a hashmap and fast lookup arrays.
DictEntry* load_dictionary(const char* dict_path, const char* lang_path, size_t* count, HashEntry** hashmap, const char mode) {
    FILE* dict_file = fopen(dict_path, "r");
    FILE* lang_file = fopen(lang_path, "r");

    if (!dict_file || !lang_file) {
        perror("Failed to open dictionary or language file");
        if (dict_file) fclose(dict_file);
        if (lang_file) fclose(lang_file);
        exit(EXIT_FAILURE);
    }

    size_t capacity = INITIAL_DICT_CAPACITY;
    DictEntry* entries = malloc(sizeof(DictEntry) * capacity);
    if (!entries) {
        perror("Memory allocation failed for dictionary entries");
        fclose(dict_file);
        fclose(lang_file);
        exit(EXIT_FAILURE);
    }

    char dict_line[MAX_LINE];
    char lang_line[MAX_LINE];
    size_t i = 0;

    while (fgets(dict_line, MAX_LINE, dict_file) && fgets(lang_line, MAX_LINE, lang_file)) {
        dict_line[strcspn(dict_line, "\n")] = 0;
        lang_line[strcspn(lang_line, "\n")] = 0;

        if (strlen(dict_line) == 0 || strlen(lang_line) == 0) {
            continue;
        }

        // Resize if needed
        if (i >= capacity) {
            capacity *= 2;
            DictEntry* new_entries = realloc(entries, sizeof(DictEntry) * capacity);
            if (!new_entries) {
                perror("Memory reallocation failed for dictionary entries");
                // Clean up what was already allocated
                for (size_t j = 0; j < i; j++) {
                    free(entries[j].word);
                    free(entries[j].symbol);
                }
                free(entries);
                fclose(dict_file);
                fclose(lang_file);
                exit(EXIT_FAILURE);
            }
            entries = new_entries;
        }

        entries[i].word = strdup(dict_line);
        entries[i].symbol = strdup(lang_line);

        if (!entries[i].word || !entries[i].symbol) {
            perror("Memory allocation failed for dictionary entry strings");
            // Clean up current entry and previous ones
            if (entries[i].word) free(entries[i].word);
            if (entries[i].symbol) free(entries[i].symbol);
            for (size_t j = 0; j < i; j++) {
                free(entries[j].word);
                free(entries[j].symbol);
            }
            free(entries);
            fclose(dict_file);
            fclose(lang_file);
            exit(EXIT_FAILURE);
        }

        HashEntry* item = malloc(sizeof(HashEntry));
        if (!item) {
            perror("Memory allocation failed for HashEntry");
            free(entries[i].word);
            free(entries[i].symbol);
            for (size_t j = 0; j < i; j++) {
                free(entries[j].word);
                free(entries[j].symbol);
            }
            free(entries);
            fclose(dict_file);
            fclose(lang_file);
            exit(EXIT_FAILURE);
        }

        if (mode == 'c') {
            item->key = entries[i].word;   // Key points to word from entries
            item->value = entries[i].symbol; // Value points to symbol from entries
            item->value_len = strlen(item->value);

            size_t slen = strlen(entries[i].symbol);
            if (slen <= 3) {
                unsigned char a = entries[i].symbol[0];
                unsigned char b = (slen > 1) ? entries[i].symbol[1] : 0;
                unsigned char c = (slen > 2) ? entries[i].symbol[2] : 0;
                symbol_lookup[a][b][c] = true;
            }
        } else { // mode == 'd'
            item->key = entries[i].symbol; // Key points to symbol from entries
            item->value = entries[i].word;   // Value points to word from entries
            item->value_len = strlen(item->value);

            size_t slen = strlen(entries[i].symbol);
            if (slen <= 3) {
                unsigned char a = entries[i].symbol[0];
                unsigned char b = (slen > 1) ? entries[i].symbol[1] : 0;
                unsigned char c = (slen > 2) ? entries[i].symbol[2] : 0;
                // Store a copy or pointer to the word, ensure it's not freed prematurely
                // Here, we store a pointer to the word string which is part of 'entries'.
                // 'entries' will be freed *after* the parallel decompression loop.
                word_lookup[a][b][c] = entries[i].word;
                word_lookup_len[a][b][c] = strlen(entries[i].word);
            }
        }
        HASH_ADD_KEYPTR(hh, *hashmap, item->key, strlen(item->key), item);
        i++;
    }

    fclose(dict_file);
    fclose(lang_file);

    *count = i;
    // Shrink to fit if capacity is much larger than actual count
    DictEntry* final_entries = realloc(entries, sizeof(DictEntry) * (*count));
    if (final_entries) {
        entries = final_entries;
    }
    return entries;
}

// Frees the memory allocated for dictionary entries.
void free_dictionary(DictEntry* entries, size_t count) {
    if (entries) {
        for (size_t i = 0; i < count; i++) {
            free(entries[i].word);
            free(entries[i].symbol);
        }
        free(entries);
    }
}

// Frees the memory allocated for the hashmap entries.
void free_hashmap(HashEntry* hashmap) {
    HashEntry* current;
    HashEntry* tmp;
    // UTHash doesn't free the key or value strings, only the HashEntry struct itself.
    // We need to free the key and value strings because they were strdup'd or part of DictEntry.
    // In this revised code, item->key and item->value now point to the strings within DictEntry.
    // So, we don't free them here to avoid double-freeing.
    // If HashEntry keys/values were strdup'd independently, they would be freed here.
    // Given the current design where they point to DictEntry strings,
    // free_dictionary handles their deallocation.
    HASH_ITER(hh, hashmap, current, tmp) {
        HASH_DEL(hashmap, current);
        free(current); // Only free the HashEntry struct itself
    }
}

// Finds an unused character in the input buffer to use as an escape character.
char find_unused_char_from_buffer(const char* buffer, size_t len) {
    bool used[256] = {0};
    used[0] = true; // Null character cannot be used
    for (size_t i = 0; i < len; i++) {
        used[(unsigned char)buffer[i]] = true;
    }
    for (int i = 1; i < 256; i++) { // Start from 1, as 0 is reserved
        if (!used[i]) return (char)i;
    }
    fprintf(stderr, "Error: No escape character available in the ASCII range.\n");
    exit(EXIT_FAILURE);
}

// Reads the entire content of a file into a buffer.
char* read_file(const char* path, const char* label, size_t* out_len) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Error: Failed to open %s file: %s\n", label, path);
        exit(EXIT_FAILURE);
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    if (length == -1) {
        perror("Error getting file length");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    rewind(file);
    char* buffer = malloc(length + 1); // +1 for null terminator
    if (!buffer) {
        perror("Memory allocation failed for file buffer");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    size_t read_bytes = fread(buffer, 1, length, file);
    if (read_bytes != (size_t)length) {
        fprintf(stderr, "Warning: Did not read entire %s file. Expected %ld, got %zu.\n", label, length, read_bytes);
    }
    buffer[read_bytes] = '\0'; // Null-terminate the buffer
    fclose(file);
    if (out_len) *out_len = read_bytes;
    return buffer;
}

// Compresses the input buffer using the provided dictionary.
void compress(const char* dict_path, const char* lang_path, const char* input_buffer, size_t input_len, int threads) {
    size_t dict_size = 0;
    HashEntry* hashmap = NULL; // Initialize hashmap to NULL for UTHash
    // Load dictionary. DictEntry pointers will be managed by `dict` and freed later.
    // HashEntry keys/values now point into `dict` strings, avoiding redundant strdups.
    DictEntry* dict = load_dictionary(dict_path, lang_path, &dict_size, &hashmap, 'c');
    char escape_char = find_unused_char_from_buffer(input_buffer, input_len);

    size_t token_count = 0;
    TokenSpan* tokens = tokenize(input_buffer, input_len, &token_count);
    if (!tokens) {
        // Tokenize exits on failure, but good practice to check if it ever returned NULL
        // (though current implementation exits).
        free_dictionary(dict, dict_size);
        free_hashmap(hashmap);
        exit(EXIT_FAILURE);
    }

    FILE* out = fopen("out", "wb");
    if (!out) {
        perror("Failed to open output file for compression");
        free(tokens);
        free_dictionary(dict, dict_size);
        free_hashmap(hashmap);
        exit(EXIT_FAILURE);
    }
    fputc(escape_char, out);

    // Allocate buffers for each thread's output segment
    char** segments = malloc(sizeof(char*) * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));
    if (!segments || !seg_lens) {
        perror("Memory allocation failed for thread segments");
        free(tokens);
        free_dictionary(dict, dict_size);
        free_hashmap(hashmap);
        if (segments) free(segments);
        if (seg_lens) free(seg_lens);
        fclose(out);
        exit(EXIT_FAILURE);
    }

    size_t tokens_per_thread = (token_count + threads - 1) / threads;

    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num();
        size_t start_idx = tid * tokens_per_thread;
        size_t end_idx = (tid + 1) * tokens_per_thread;
        if (end_idx > token_count) end_idx = token_count;

        // Allocate a buffer for this thread's output. Max possible expansion is `input_len * 4`
        // if every char became an escaped char and a replacement.
        // Add 1024 for safety margin.
        char* buffer = malloc((end_idx - start_idx) * 4 + 1024);
        if (!buffer) {
            fprintf(stderr, "Thread %d: Memory allocation failed for output buffer\n", tid);
            // Cannot directly exit from parallel region; handle error propagation
            // For simplicity in this example, we'll let it crash, or you could use a flag
            // and `omp barrier` to check for errors before proceeding.
            exit(EXIT_FAILURE);
        }
        size_t out_pos = 0;

        for (size_t i = start_idx; i < end_idx; i++) {
            TokenSpan tok = tokens[i];
            const char* ptr = &input_buffer[tok.start];

            if (tok.is_space) {
                // Copy delimiters directly
                memcpy(&buffer[out_pos], ptr, tok.len);
                out_pos += tok.len;
            } else {
                // For words, create a temporary null-terminated string for hash lookup
                char* temp = malloc(tok.len + 1);
                if (!temp) {
                    fprintf(stderr, "Thread %d: Memory allocation failed for temp token\n", tid);
                    exit(EXIT_FAILURE);
                }
                memcpy(temp, ptr, tok.len);
                temp[tok.len] = '\0';

                HashEntry* found = NULL;
                HASH_FIND_STR(hashmap, temp, found); // Use global hashmap
                bool needs_escape = is_symbol_fast(temp, tok.len); // Use global symbol_lookup

                if (found) {
                    // Replace word with its symbol
                    memcpy(&buffer[out_pos], found->value, found->value_len);
                    out_pos += found->value_len;
                } else if (needs_escape) {
                    // If original word is a symbol, escape it
                    buffer[out_pos++] = escape_char;
                    memcpy(&buffer[out_pos], temp, tok.len);
                    out_pos += tok.len;
                } else {
                    // Not found and not an existing symbol, copy as is
                    memcpy(&buffer[out_pos], temp, tok.len);
                    out_pos += tok.len;
                }
                free(temp); // Free temporary string
            }
        }
        segments[tid] = buffer;
        seg_lens[tid] = out_pos;
    } // End of parallel region

    // Write all segments to the output file sequentially
    for (int i = 0; i < threads; i++) {
        fwrite(segments[i], 1, seg_lens[i], out);
        free(segments[i]); // Free thread-specific buffer
    }

    fclose(out);
    free(segments);
    free(seg_lens);
    free(tokens); // Free token spans
    free_dictionary(dict, dict_size); // Free dictionary entries (word/symbol strings)
    free_hashmap(hashmap); // Free HashEntry structs (but not their key/value strings as they point to dict)
}


// Decompresses the input buffer using the provided dictionary.
void decompress(const char* dict_path, const char* lang_path,
                const char* input_buffer, size_t input_len, int threads) {
    if (input_len < 1) return; // Nothing to decompress

    size_t dict_size = 0;
    HashEntry* hashmap = NULL;
    // Load dictionary. Keys are symbols, values are words.
    // `word_lookup` will be populated here with pointers to `DictEntry.word` strings.
    DictEntry* dict = load_dictionary(lang_path, dict_path, &dict_size, &hashmap, 'd');

    char escape_char = input_buffer[0];
    const char* data = input_buffer + 1; // Actual compressed data starts after escape char
    size_t data_len = input_len - 1;

    FILE* out = fopen("out_decompressed", "wb");
    if (!out) {
        perror("Failed to open decompressed output file");
        free_dictionary(dict, dict_size);
        free_hashmap(hashmap);
        exit(EXIT_FAILURE);
    }

    // Calculate approximate work distribution for splitting input
    size_t* split_points = malloc(sizeof(size_t) * (threads + 1));
    if (!split_points) {
        perror("Memory allocation failed for split_points");
        free_dictionary(dict, dict_size);
        free_hashmap(hashmap);
        fclose(out);
        exit(EXIT_FAILURE);
    }

    split_points[0] = 0;
    split_points[threads] = data_len;

    // Find actual split points at token boundaries to avoid splitting a token
    int actual_threads = threads; // Keep track of potentially reduced thread count
    for (int t = 1; t < threads; t++) {
        size_t approx_pos = t * (data_len / threads); // More precise initial split
        if (approx_pos >= data_len) {
            // Adjust for case where we have more threads than needed
            for (int remaining = t; remaining <= threads; remaining++) {
                split_points[remaining] = data_len;
            }
            actual_threads = t; // Reduce effective thread count
            break;
        }

        // Search for next delimiter to ensure we split at a token boundary
        // Search forwards first, then backwards if end of segment is found
        size_t search_pos = approx_pos;
        while (search_pos < data_len && !is_delimiter(data[search_pos]) && data[search_pos] != escape_char) {
            search_pos++;
        }
        if (search_pos == data_len) { // Reached end, backtrack for a delimiter
             search_pos = approx_pos;
             while (search_pos > split_points[t-1] && !is_delimiter(data[search_pos]) && data[search_pos] != escape_char) {
                search_pos--;
             }
             if (search_pos == split_points[t-1] && !is_delimiter(data[search_pos])) {
                 // If no delimiter found between current and previous split, take approx_pos
                 split_points[t] = approx_pos;
             } else {
                 split_points[t] = search_pos;
             }
        } else {
            split_points[t] = search_pos;
        }
    }

    char** segments = malloc(sizeof(char*) * actual_threads);
    size_t* seg_lens = calloc(actual_threads, sizeof(size_t));
    if (!segments || !seg_lens) {
        perror("Memory allocation failed for thread segments");
        free(split_points);
        free_dictionary(dict, dict_size);
        free_hashmap(hashmap);
        fclose(out);
        if (segments) free(segments);
        if (seg_lens) free(seg_lens);
        exit(EXIT_FAILURE);
    }

    #pragma omp parallel num_threads(actual_threads)
    {
        int tid = omp_get_thread_num();
        size_t start_pos = split_points[tid];
        size_t end_pos = split_points[tid + 1];

        // Allocate a buffer for this thread's output. Max possible expansion is `input_len * 4`
        // Add 1024 for safety margin.
        char* buffer = malloc((end_pos - start_pos) * 4 + 1024);
        if (!buffer) {
            fprintf(stderr, "Thread %d: Memory allocation failed for output buffer\n", tid);
            exit(EXIT_FAILURE);
        }
        size_t out_pos = 0;
        size_t i = start_pos;

        while (i < end_pos) {
            // Handle delimiters (spaces, punctuation, etc.)
            if (is_delimiter(data[i])) {
                buffer[out_pos++] = data[i];
                i++;
                continue;
            }

            // Process non-delimiter token
            size_t token_start = i;
            bool is_escaped = false;

            // Check for escape character at the beginning of a potential token
            if (data[token_start] == escape_char) {
                is_escaped = true;
                token_start++; // Move past the escape character
                // Ensure we don't go out of bounds or process only escape char
                if (token_start >= end_pos) {
                    // This could happen if escape char is at the very end of a segment
                    // or if it's an invalid input. Copy as is.
                    buffer[out_pos++] = escape_char;
                    i++;
                    continue;
                }
            }

            // Find end of current token
            // A token either ends at a delimiter or the escape character
            size_t current_token_end = token_start;
            while (current_token_end < end_pos &&
                   !is_delimiter(data[current_token_end]) &&
                   (!is_escaped || data[current_token_end] != escape_char)) {
                current_token_end++;
            }

            size_t actual_len = current_token_end - token_start;
            const char* actual_token_ptr = &data[token_start];

            // If the token length is 0 (e.g., just an escape char followed by delimiter),
            // or if it's the escape char itself not followed by anything, handle it.
            if (actual_len == 0 && is_escaped) {
                 // This means `data[token_start]` was a delimiter or end of segment
                 // after an escape char. Copy escape char and advance.
                 buffer[out_pos++] = escape_char;
                 i = current_token_end; // Advance 'i' past the escape char
                 continue;
            }


            // Try fast lookup for short symbols (1-3 chars) if not escaped
            if (!is_escaped && actual_len > 0 && actual_len <= 3) {
                unsigned char a = actual_token_ptr[0];
                unsigned char b = (actual_len > 1) ? actual_token_ptr[1] : 0;
                unsigned char c = (actual_len > 2) ? actual_token_ptr[2] : 0;
                char* replacement = word_lookup[a][b][c];

                if (replacement) {
                    size_t repl_len = word_lookup_len[a][b][c];
                    memcpy(&buffer[out_pos], replacement, repl_len);
                    out_pos += repl_len;
                    i = current_token_end; // Advance 'i' past the processed symbol
                    continue;
                }
            }

            // Fallback: hash table lookup for longer symbols or if fast lookup failed
            if (!is_escaped && actual_len > 0) { // Only lookup if not escaped
                char* temp_token = malloc(actual_len + 1);
                if (!temp_token) {
                    fprintf(stderr, "Thread %d: Memory allocation failed for temp token\n", tid);
                    exit(EXIT_FAILURE);
                }
                memcpy(temp_token, actual_token_ptr, actual_len);
                temp_token[actual_len] = '\0';

                HashEntry* found = NULL;
                HASH_FIND_STR(hashmap, temp_token, found);

                if (found) {
                    memcpy(&buffer[out_pos], found->value, found->value_len);
                    out_pos += found->value_len;
                    free(temp_token);
                    i = current_token_end; // Advance 'i' past the processed symbol
                    continue;
                }
                free(temp_token);
            }

            // No replacement found or it was an escaped token, copy original (or escaped original)
            memcpy(&buffer[out_pos], actual_token_ptr, actual_len);
            out_pos += actual_len;
            i = current_token_end; // Advance 'i' past the copied token
        }

        segments[tid] = buffer;
        seg_lens[tid] = out_pos;
    } // End of parallel region

    // Write all segments to output file sequentially
    for (int i = 0; i < actual_threads; i++) {
        fwrite(segments[i], 1, seg_lens[i], out);
        free(segments[i]); // Free thread-specific buffer
    }

    fclose(out);
    free(segments);
    free(seg_lens);
    free(split_points);
    free_dictionary(dict, dict_size); // Free dictionary entries
    free_hashmap(hashmap); // Free HashEntry structs
}

int main(int argc, char* argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <-c|-d> <file_path> <dictionary_file_path> <language_file_path> <thread_count>\n", argv[0]);
        fprintf(stderr, "  -c:  compress\n");
        fprintf(stderr, "  -d:  decompress\n");
        return 1;
    }

    const char* mode_flag = argv[1];
    const char* file_path = argv[2];
    const char* dict_path = argv[3];
    const char* language_path = argv[4];
    int threads = atoi(argv[5]);

    if (threads <= 0) {
        fprintf(stderr, "Error: Thread count must be a positive integer.\n");
        return 1;
    }

    size_t input_len = 0;
    char* input_buffer = read_file(file_path, "Input", &input_len);
    if (!input_buffer) {
        // read_file exits on failure, but defensive check
        return 1;
    }

    // Initialize global lookup tables to zero/NULL.
    // This is important because the same process might run compress then decompress,
    // or if the main function logic were to change.
    // For static storage duration arrays, they are zero-initialized by default,
    // but explicit clearing provides clarity and safety if used in a loop.
    memset(symbol_lookup, 0, sizeof(symbol_lookup));
    memset(word_lookup, 0, sizeof(word_lookup));
    memset(word_lookup_len, 0, sizeof(word_lookup_len));


    if (strcmp(mode_flag, "-c") == 0) {
        compress(dict_path, language_path, input_buffer, input_len, threads);
    } else if (strcmp(mode_flag, "-d") == 0) {
        decompress(language_path, dict_path, input_buffer, input_len, threads);
    } else {
        fprintf(stderr, "Error: Invalid mode. Use -c for compress or -d for decompress.\n");
        free(input_buffer);
        return 1;
    }

    free(input_buffer);
    return 0;
}
