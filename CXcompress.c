// Includes and Definitions
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

// Structs
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

// Utility Functions
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
    char dict_line[MAX_LINE], lang_line[MAX_LINE];
    size_t i = 0;

    while (fgets(dict_line, MAX_LINE, dict_file) && fgets(lang_line, MAX_LINE, lang_file)) {
        dict_line[strcspn(dict_line, "\n")] = 0;
        lang_line[strcspn(lang_line, "\n")] = 0;
        if (strlen(dict_line) == 0 || strlen(lang_line) == 0) continue;

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
    HashEntry* current; HashEntry* tmp;
    HASH_ITER(hh, hashmap, current, tmp) {
        HASH_DEL(hashmap, current);
        free(current->key);
        free(current);
    }
}

char find_unused_char_from_buffer(const char* buffer, size_t len) {
    bool used[256] = {0}; used[0] = true;
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
    buffer[read] = '\0'; fclose(file);
    if (out_len) *out_len = read;
    return buffer;
}

// Updated compression function to support 4-byte space counts
void compress(const char* dict_path, const char* lang_path, const char* input_buffer, size_t input_len, int threads) {
    size_t dict_size = 0;
    HashEntry* hashmap = NULL;
    DictEntry* dict = load_dictionary(dict_path, lang_path, &dict_size, &hashmap, 'c');
    char escape_char = find_unused_char_from_buffer(input_buffer, input_len);

    size_t token_count = 0, leading_spaces = 0, trailing_spaces = 0;
    for (size_t i = 0; i < input_len && input_buffer[i] == ' '; i++) leading_spaces++;
    for (size_t i = input_len; i > 0 && input_buffer[i - 1] == ' '; i--) trailing_spaces++;

    bool in_space = false;
    for (size_t i = leading_spaces; i < input_len - trailing_spaces; i++) {
        if (input_buffer[i] == ' ') {
            in_space = true;
        } else {
            if (in_space || i == leading_spaces) token_count++;
            in_space = false;
        }
    }

    Token* tokens = malloc(sizeof(Token) * token_count);
    size_t* spaces_before = calloc(token_count, sizeof(size_t));
    size_t current_token = 0, current_spaces = 0;
    in_space = false;

    for (size_t i = leading_spaces; i < input_len - trailing_spaces;) {
        if (input_buffer[i] == ' ') {
            current_spaces++;
            i++;
            in_space = true;
            continue;
        }
        if (in_space || current_token == 0) {
            if (current_token > 0) spaces_before[current_token] = current_spaces;
            current_spaces = 0;
            size_t start = i;
            while (i < input_len - trailing_spaces && input_buffer[i] != ' ') i++;
            size_t len = i - start;
            bool escaped = is_symbol_fast(&input_buffer[start], len);
            tokens[current_token++] = (Token){ .start = &input_buffer[start], .len = len, .escaped = escaped };
            in_space = false;
        }
    }

    FILE* out = fopen("out", "wb");
    fwrite(&escape_char, 1, 1, out);
    fwrite(&leading_spaces, sizeof(uint32_t), 1, out);
    fwrite(&trailing_spaces, sizeof(uint32_t), 1, out);

    char** segments = malloc(sizeof(char*) * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));

    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num();
        size_t start = (token_count * tid) / threads;
        size_t end = (token_count * (tid + 1)) / threads;
        char* buffer = malloc((end - start) * 16 + input_len);
        size_t out_pos = 0;

        for (size_t j = start; j < end; j++) {
            for (size_t s = 0; s < spaces_before[j]; s++) buffer[out_pos++] = ' ';
            Token tok = tokens[j];
            char temp[256];
            memcpy(temp, tok.start, tok.len); temp[tok.len] = '\0';
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
    free(segments); free(seg_lens); free(tokens); free(spaces_before);
    free_dictionary(dict, dict_size);
    free_hashmap(hashmap);
}

void decompress(const char* dict_path, const char* lang_path, const char* input_buffer, size_t input_len, int threads) {
    size_t dict_size = 0;
    HashEntry* hashmap = NULL;
    DictEntry* dict = load_dictionary(lang_path, dict_path, &dict_size, &hashmap, 'd');

    if (input_len < 9) { fprintf(stderr, "Invalid input file\n"); return; }

    char escape_char = input_buffer[0];
    uint32_t leading_spaces, trailing_spaces;
    memcpy(&leading_spaces, input_buffer + 1, sizeof(uint32_t));
    memcpy(&trailing_spaces, input_buffer + 5, sizeof(uint32_t));
    const char* data = input_buffer + 9;
    size_t data_len = input_len - 9;

    FILE* out = fopen("out_decompressed", "wb");
    for (uint32_t i = 0; i < leading_spaces; i++) fputc(' ', out);

    fwrite(data, 1, data_len, out);

    for (uint32_t i = 0; i < trailing_spaces; i++) fputc(' ', out);
    fclose(out);
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
    else { fprintf(stderr, "Invalid mode\n"); free(input_buffer); return 1; }

    free(input_buffer);
    return 0;
}
