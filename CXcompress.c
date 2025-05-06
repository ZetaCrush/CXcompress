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

typedef struct {
    char* word;
    char* symbol;
} DictEntry;

typedef struct {
    char* key;
    char* value;
    UT_hash_handle hh;
} HashEntry;

typedef struct {
    const char* start;
    size_t len;
    bool escaped;
} Token;

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
            item->value = entries[i].symbol;
            size_t slen = strlen(entries[i].symbol);
            if (slen <= 3) {
                unsigned char a = entries[i].symbol[0];
                unsigned char b = (slen > 1) ? entries[i].symbol[1] : 0;
                unsigned char c = (slen > 2) ? entries[i].symbol[2] : 0;
                symbol_lookup[a][b][c] = true;
            }
        } else {
            item->key = strdup(entries[i].symbol);
            item->value = entries[i].word;
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
    char* buffer = malloc(length);
    fread(buffer, 1, length, file); fclose(file);
    if (out_len) *out_len = length;
    return buffer;
}

void compress(const char* dict_path, const char* lang_path, const char* input_buffer, size_t input_len, int threads) {
    size_t dict_size = 0;
    HashEntry* hashmap = NULL;
    DictEntry* dict = load_dictionary(dict_path, lang_path, &dict_size, &hashmap, 'c');
    char escape_char = find_unused_char_from_buffer(input_buffer, input_len);

    // First pass to count tokens and spaces
    size_t token_count = 0;
    size_t space_count = 0;
    bool in_space = false;
    for (size_t i = 0; i < input_len; i++) {
        if (input_buffer[i] == ' ') {
            if (!in_space) {
                space_count++;
                in_space = true;
            }
        } else {
            if (in_space || i == 0) {
                token_count++;
            }
            in_space = false;
        }
    }

    Token* tokens = malloc(sizeof(Token) * token_count);
    size_t* spaces_before = calloc(token_count, sizeof(size_t));
    size_t current_token = 0;
    in_space = false;
    size_t current_spaces = 0;

    // Second pass to record tokens and spaces
    for (size_t i = 0; i < input_len; ) {
        if (input_buffer[i] == ' ') {
            current_spaces++;
            i++;
            in_space = true;
            continue;
        }

        if (in_space || current_token == 0) {
            if (current_token > 0) {
                spaces_before[current_token] = current_spaces;
            }
            current_spaces = 0;

            size_t start = i;
            while (i < input_len && input_buffer[i] != ' ') i++;
            size_t len = i - start;

            bool escaped = is_symbol_fast(&input_buffer[start], len);
            tokens[current_token++] = (Token){ .start = &input_buffer[start], .len = len, .escaped = escaped };
            in_space = false;
        }
    }

    FILE* out = fopen("out", "wb");
    fputc(escape_char, out);
    char** segments = malloc(sizeof(char*) * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));

    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num();
        size_t start = (token_count * tid) / threads;
        size_t end = (token_count * (tid + 1)) / threads;
        char* buffer = malloc((end - start) * 16 + input_len); // Extra space for worst case
        size_t out_pos = 0;

        for (size_t j = start; j < end; j++) {
            // Add spaces before token
            for (size_t s = 0; s < spaces_before[j]; s++) {
                buffer[out_pos++] = ' ';
            }

            // Add the token itself
            Token tok = tokens[j];
            char temp[256];
            memcpy(temp, tok.start, tok.len);
            temp[tok.len] = '\0';
            HashEntry* found = NULL;
            HASH_FIND_STR(hashmap, temp, found);
            if (found) {
                size_t slen = strlen(found->value);
                memcpy(&buffer[out_pos], found->value, slen);
                out_pos += slen;
            } else if (tok.escaped) {
                buffer[out_pos++] = escape_char;
                memcpy(&buffer[out_pos], tok.start, tok.len);
                out_pos += tok.len;
            } else {
                memcpy(&buffer[out_pos], tok.start, tok.len);
                out_pos += tok.len;
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
    free(tokens);
    free(spaces_before);
    for (size_t i = 0; i < dict_size; i++) {
        free(dict[i].word);
        free(dict[i].symbol);
    }
    free(dict);
    HashEntry *cur, *tmp;
    HASH_ITER(hh, hashmap, cur, tmp) {
        HASH_DEL(hashmap, cur);
        free(cur->key);
        free(cur);
    }
}

void decompress(const char* dict_path, const char* lang_path, const char* input_buffer, size_t input_len, int threads) {
    size_t dict_size = 0;
    HashEntry* hashmap = NULL;
    DictEntry* dict = load_dictionary(lang_path, dict_path, &dict_size, &hashmap, 'd');

    if (input_len < 1) {
        fprintf(stderr, "Empty input file\n");
        free_dictionary(dict, dict_size);
        free_hashmap(hashmap);
        return;
    }

    char escape_char = input_buffer[0];
    const char* data = input_buffer + 1;
    size_t data_len = input_len - 1;

    // First pass to count tokens and spaces
    size_t token_count = 0;
    size_t space_count = 0;
    bool in_space = false;
    for (size_t i = 0; i < data_len; i++) {
        if (data[i] == ' ') {
            if (!in_space) {
                space_count++;
                in_space = true;
            }
        } else {
            if (in_space || i == 0) {
                token_count++;
            }
            in_space = false;
        }
    }

    FILE* out = fopen("out_decompressed", "wb");
    if (!out) {
        fprintf(stderr, "Failed to open decompressed output file\n");
        free_dictionary(dict, dict_size);
        free_hashmap(hashmap);
        exit(1);
    }

    char** segments = malloc(sizeof(char*) * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));
    size_t* seg_space_counts = calloc(threads, sizeof(size_t));

    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num();
        size_t start = (data_len * tid) / threads;
        size_t end = (data_len * (tid + 1)) / threads;

        // Align to token boundaries
        while (start > 0 && data[start] != ' ') start++;
        while (end < data_len && data[end] != ' ') end++;

        char* buffer = malloc((end - start + 1) * 8 + data_len); // Extra space for worst case
        size_t out_pos = 0;
        size_t space_count = 0;
        bool in_space = false;

        for (size_t i = start; i < end; ) {
            if (data[i] == ' ') {
                if (!in_space) {
                    space_count = 0;
                    in_space = true;
                }
                space_count++;
                buffer[out_pos++] = ' ';
                i++;
                continue;
            }

            in_space = false;
            bool is_escaped = (data[i] == escape_char);
            if (is_escaped) i++;

            size_t s = i;
            while (i < end && data[i] != ' ') i++;
            size_t len = i - s;

            if (len > 0) {
                char* token = strndup(&data[s], len);
                char* output = NULL;
                size_t output_len = 0;

                if (!is_escaped) {
                    HashEntry* found = NULL;
                    HASH_FIND_STR(hashmap, token, found);
                    if (found) {
                        output = found->value;
                        output_len = strlen(output);
                    } else {
                        output = token;
                        output_len = len;
                    }
                } else {
                    output = token;
                    output_len = len;
                }

                memcpy(&buffer[out_pos], output, output_len);
                out_pos += output_len;
                free(token);
            }
        }

        segments[tid] = buffer;
        seg_lens[tid] = out_pos;
        seg_space_counts[tid] = space_count;
    }

    // Combine segments
    for (int i = 0; i < threads; i++) {
        fwrite(segments[i], 1, seg_lens[i], out);
        free(segments[i]);
    }

    fclose(out);
    free(segments);
    free(seg_lens);
    free(seg_space_counts);
    free_dictionary(dict, dict_size);
    free_hashmap(hashmap);
}

int main(int argc, char* argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <-c|-d> <file_path> <dictionary_file_path> <language_file_path> <thread_count>\n", argv[0]);
        return 1;
    }
    const char* mode_flag = argv[1];
    const char* file_path = argv[2];
    const char* dict_path = argv[3];
    const char* language_path = argv[4];
    int threads = atoi(argv[5]);

    size_t input_len = 0;
    char* input_buffer = read_file(file_path, "Input", &input_len);

    if (strcmp(mode_flag, "-c") == 0) compress(dict_path, language_path, input_buffer, input_len, threads);
    else if (strcmp(mode_flag, "-d") == 0) decompress(language_path, dict_path, input_buffer, input_len, threads);
    else { fprintf(stderr, "Invalid mode\n"); return 1; }

    free(input_buffer);
    return 0;
}

