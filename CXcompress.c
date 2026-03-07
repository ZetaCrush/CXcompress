#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <omp.h>
#include "uthash.h"

#define MAX_LINE 1024
#define MAX_ENTRIES 100000

bool symbol_lookup[256][256][256] = {{{ false }}};
char* word_lookup[256][256][256] = {{{ NULL }}};
unsigned char word_lookup_len[256][256][256] = {{{ 0 }}};

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

typedef struct {
    const char* start;
    size_t len;
    bool escaped;
} Token;

typedef struct {
    size_t start;
    size_t len;
    bool is_space;
} TokenSpan;

// Helper function to check if character is a delimiter
static inline bool is_delimiter(char c) {
    return (c == ' ' || c == 0 || c == ',' || c == '.' ||
            c == '?' || c == '!' || c == '\n' || c == '\r');
}

TokenSpan* tokenize(const char* input, size_t len, size_t* token_count_out) {
    size_t capacity = 1024;
    TokenSpan* spans = malloc(sizeof(TokenSpan) * capacity);
    if (!spans) {
        fprintf(stderr, "Memory allocation failed for tokenization\n");
        exit(1);
    }

    size_t i = 0, count = 0;

    while (i < len) {
        if (count >= capacity) {
            capacity *= 2;
            spans = realloc(spans, sizeof(TokenSpan) * capacity);
            if (!spans) {
                fprintf(stderr, "Memory reallocation failed for tokenization\n");
                exit(1);
            }
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

bool is_symbol_fast(const char* word, size_t len) {
    if (len == 0 || len > 3) return false;
    unsigned char a = word[0];
    unsigned char b = (len > 1) ? word[1] : 0;
    unsigned char c = (len > 2) ? word[2] : 0;
    return symbol_lookup[a][b][c];
}

DictEntry* load_dictionary(const char* dict_path, const char* lang_path, size_t* count, HashEntry** hashmap, const char mode) {
    FILE* dict_file = fopen(dict_path, "r");
    FILE* lang_file = fopen(lang_path, "r");

    if (!dict_file || !lang_file) {
        fprintf(stderr, "Failed to open dictionary (%s) or language file (%s)\n", dict_path, lang_path);
        exit(1);
    }

    DictEntry* entries = malloc(sizeof(DictEntry) * MAX_ENTRIES);
    if (!entries) {
        fprintf(stderr, "Memory allocation failed for dictionary\n");
        exit(1);
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

        entries[i].word = strdup(dict_line);
        entries[i].symbol = strdup(lang_line);

        HashEntry* item = malloc(sizeof(HashEntry));
        if (mode == 'c') {
            item->key = strdup(entries[i].word);
            item->value = strdup(entries[i].symbol);
            item->value_len = strlen(item->value);
            size_t slen = strlen(entries[i].symbol);
            if (slen <= 3) {
                unsigned char a = entries[i].symbol[0];
                unsigned char b = (slen > 1) ? entries[i].symbol[1] : 0;
                unsigned char c = (slen > 2) ? entries[i].symbol[2] : 0;
                symbol_lookup[a][b][c] = true;
            }
        } else {
            item->key = strdup(entries[i].symbol);
            item->value = strdup(entries[i].word);
            item->value_len = strlen(item->value);

            size_t slen = strlen(entries[i].symbol);
            if (slen <= 3) {
                unsigned char a = entries[i].symbol[0];
                unsigned char b = (slen > 1) ? entries[i].symbol[1] : 0;
                unsigned char c = (slen > 2) ? entries[i].symbol[2] : 0;
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
    return entries;
}

void free_dictionary(DictEntry* entries, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(entries[i].word);
        free(entries[i].symbol);
    }
    free(entries);
}

void free_hashmap(HashEntry* hashmap) {
    HashEntry* current;
    HashEntry* tmp;
    HASH_ITER(hh, hashmap, current, tmp) {
        HASH_DEL(hashmap, current);
        free(current->key);
        free(current->value);
        free(current);
    }
}

char find_unused_char_from_buffer(const char* buffer, size_t len) {
    bool used[256] = {0};
    used[0] = true;
    for (size_t i = 0; i < len; i++) used[(unsigned char)buffer[i]] = true;
    for (int i = 0; i < 256; i++) if (!used[i]) return (char)i;
    fprintf(stderr, "No escape character available\n"); exit(1);
}

char* read_file(const char* path, const char* label, size_t* out_len) {
    FILE* file = fopen(path, "rb");
    if (!file) { fprintf(stderr, "Failed to open %s file: %s\n", label, path); exit(1); }
    fseek(file, 0, SEEK_END); long length = ftell(file); rewind(file);
    char* buffer = malloc(length + 1);
    size_t read = fread(buffer, 1, length, file);
    buffer[read] = '\0';
    fclose(file);
    if (out_len) *out_len = read;
    return buffer;
}

// Add this global (or pass it through) to match your decompression style
char* compress_lookup[256][256][256] = {{{ NULL }}};
unsigned char compress_lookup_len[256][256][256] = {{{ 0 }}};

void compress(const char* dict_path, const char* lang_path, const char* input_buffer, size_t input_len, int threads, const char* output_path) {
    size_t dict_size = 0;
    HashEntry* hashmap = NULL;
    // Load dict once
    DictEntry* dict = load_dictionary(dict_path, lang_path, &dict_size, &hashmap, 'c');
    char escape_char = find_unused_char_from_buffer(input_buffer, input_len);

    FILE* out = fopen(output_path, "wb");
    fputc(escape_char, out);

    // Calculate split points exactly like your decompression function
    size_t bytes_per_thread = (input_len + threads - 1) / threads;
    size_t* split_points = malloc(sizeof(size_t) * (threads + 1));
    split_points[0] = 0;
    split_points[threads] = input_len;

    for (int t = 1; t < threads; t++) {
        size_t approx_pos = t * bytes_per_thread;
        while (approx_pos < input_len && !is_delimiter(input_buffer[approx_pos])) {
            approx_pos++;
        }
        split_points[t] = (approx_pos > input_len) ? input_len : approx_pos;
    }

    char** segments = malloc(sizeof(char*) * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));

    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num();
        size_t start_pos = split_points[tid];
        size_t end_pos = split_points[tid + 1];

        // Pre-allocate thread-local output buffer
        char* buffer = malloc((end_pos - start_pos) * 2 + 1024);
        size_t out_pos = 0;
        size_t i = start_pos;

        while (i < end_pos) {
            // Handle delimiters (Spaces/Punctuation)
            if (is_delimiter(input_buffer[i])) {
                buffer[out_pos++] = input_buffer[i];
                i++;
                continue;
            }

            // Identify word boundaries
            size_t word_start = i;
            while (i < end_pos && !is_delimiter(input_buffer[i])) {
                i++;
            }
            size_t word_len = i - word_start;
            const char* word_ptr = &input_buffer[word_start];

            // FAST PATH: Use your 3D lookup for short words (1-3 chars)
            bool found_fast = false;
            if (word_len <= 3) {
                unsigned char a = word_ptr[0];
                unsigned char b = (word_len > 1) ? word_ptr[1] : 0;
                unsigned char c = (word_len > 2) ? word_ptr[2] : 0;

                // Note: You must populate a 'compress_lookup' table in load_dictionary
                if (word_lookup[a][b][c]) {
                    // ... implementation of O(1) jump ...
                }
            }

            if (!found_fast) {
                // STACK ALLOCATION: No malloc inside this loop!
                char temp[256];
                size_t copy_len = (word_len < 255) ? word_len : 255;
                memcpy(temp, word_ptr, copy_len);
                temp[copy_len] = '\0';

                HashEntry* found = NULL;
                HASH_FIND_STR(hashmap, temp, found);

                if (found) {
                    memcpy(&buffer[out_pos], found->value, found->value_len);
                    out_pos += found->value_len;
                } else {
                    // Check if the word itself looks like a symbol
                    if (is_symbol_fast(temp, word_len)) {
                        buffer[out_pos++] = escape_char;
                    }
                    memcpy(&buffer[out_pos], word_ptr, word_len);
                    out_pos += word_len;
                }
            }
        }
        segments[tid] = buffer;
        seg_lens[tid] = out_pos;
    }

    for (int i = 0; i < threads; i++) {
        fwrite(segments[i], 1, seg_lens[i], out);
        free(segments[i]);
    }

    fclose(out);
    free(segments);
    free(seg_lens);
    free_dictionary(dict, dict_size);
    free_hashmap(hashmap);
}

void decompress(const char* dict_path, const char* lang_path,
                           const char* input_buffer, size_t input_len, int threads, const char* output_path) {
    if (input_len < 1) return;

    size_t dict_size = 0;
    HashEntry* hashmap = NULL;
    DictEntry* dict = load_dictionary(lang_path, dict_path, &dict_size, &hashmap, 'd');

    char escape_char = input_buffer[0];
    const char* data = input_buffer + 1;
    size_t data_len = input_len - 1;

    FILE* out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "Failed to open decompressed output file: %s\n", output_path);
        exit(1);
    }

    size_t bytes_per_thread = (data_len + threads - 1) / threads;

    size_t* split_points = malloc(sizeof(size_t) * (threads + 1));
    split_points[0] = 0;
    split_points[threads] = data_len;

    for (int t = 1; t < threads; t++) {
        size_t approx_pos = t * bytes_per_thread;
        if (approx_pos >= data_len) {
            for (int remaining = t; remaining <= threads; remaining++) {
                split_points[remaining] = data_len;
            }
            threads = t;
            break;
        }

        while (approx_pos < data_len && !is_delimiter(data[approx_pos])) {
            approx_pos++;
        }
        split_points[t] = approx_pos;
    }

    char** segments = malloc(sizeof(char*) * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));

    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num();
        size_t start_pos = split_points[tid];
        size_t end_pos = split_points[tid + 1];

        char* buffer = malloc((end_pos - start_pos) * 4 + 1024);
        size_t out_pos = 0;
        size_t i = start_pos;

        while (i < end_pos) {
            if (is_delimiter(data[i])) {
                buffer[out_pos++] = data[i];
                i++;
                continue;
            }

            size_t token_start = i;
            while (i < end_pos && !is_delimiter(data[i])) {
                i++;
            }

            size_t token_len = i - token_start;
            const char* token_ptr = &data[token_start];

            bool is_escaped = (token_ptr[0] == escape_char);
            const char* actual_token = is_escaped ? token_ptr + 1 : token_ptr;
            size_t actual_len = token_len - (is_escaped ? 1 : 0);

            if (!is_escaped && actual_len <= 3) {
                unsigned char a = actual_token[0];
                unsigned char b = (actual_len > 1) ? actual_token[1] : 0;
                unsigned char c = (actual_len > 2) ? actual_token[2] : 0;
                char* replacement = word_lookup[a][b][c];

                if (replacement) {
                    size_t repl_len = word_lookup_len[a][b][c];
                    memcpy(&buffer[out_pos], replacement, repl_len);
                    out_pos += repl_len;
                    continue;
                }
            }

            if (!is_escaped) {
                char* temp_token = malloc(actual_len + 1);
                memcpy(temp_token, actual_token, actual_len);
                temp_token[actual_len] = '\0';

                HashEntry* found = NULL;
                HASH_FIND_STR(hashmap, temp_token, found);

                if (found) {
                    memcpy(&buffer[out_pos], found->value, found->value_len);
                    out_pos += found->value_len;
                    free(temp_token);
                    continue;
                }
                free(temp_token);
            }

            memcpy(&buffer[out_pos], actual_token, actual_len);
            out_pos += actual_len;
        }

        segments[tid] = buffer;
        seg_lens[tid] = out_pos;
    }

    for (int i = 0; i < threads; i++) {
        fwrite(segments[i], 1, seg_lens[i], out);
        free(segments[i]);
    }

    fclose(out);
    free(segments);
    free(seg_lens);
    free(split_points);
    free_dictionary(dict, dict_size);
    free_hashmap(hashmap);
}

int main(int argc, char* argv[]) {
    // Required arguments: mode, input, dict, lang, threads, output (6 total)
    if (argc != 7) {
        fprintf(stderr, "Usage: %s <-c|-d> <input_file> <dict_file> <lang_file> <threads> <output_file>\n", argv[0]);
        fprintf(stderr, "  -c:  compress\n");
        fprintf(stderr, "  -d:  decompress\n");
        return 1;
    }
    const char* mode_flag = argv[1];
    const char* file_path = argv[2];
    const char* dict_path = argv[3];
    const char* language_path = argv[4];
    int threads = atoi(argv[5]);
    const char* output_path = argv[6];

    size_t input_len = 0;
    char* input_buffer = read_file(file_path, "Input", &input_len);

    if (strcmp(mode_flag, "-c") == 0) {
        compress(dict_path, language_path, input_buffer, input_len, threads, output_path);
    } else if (strcmp(mode_flag, "-d") == 0) {
        decompress(language_path, dict_path, input_buffer, input_len, threads, output_path);
    } else {
        fprintf(stderr, "Invalid mode\n");
        free(input_buffer);
        return 1;
    }

    free(input_buffer);
    return 0;
}
