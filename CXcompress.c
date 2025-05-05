#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <omp.h>
#include "uthash.h"  // Add uthash for O(1) dictionary lookup

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
        fprintf(stderr, "Failed to open dictionary or language file\n");
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
        entries[i].word = strdup(dict_line);
        entries[i].symbol = strdup(lang_line);

        if (mode=='c') {
            // Add to symbol lookup
            size_t slen = strlen(entries[i].symbol);
            unsigned char a = entries[i].symbol[0];
            unsigned char b = (slen > 1) ? entries[i].symbol[1] : 0;
            unsigned char c = (slen > 2) ? entries[i].symbol[2] : 0;
            symbol_lookup[a][b][c] = true;
        } else {
            // Add to symbol lookup
            size_t slen = strlen(entries[i].word);
            unsigned char a = entries[i].word[0];
            unsigned char b = (slen > 1) ? entries[i].word[1] : 0;
            unsigned char c = (slen > 2) ? entries[i].word[2] : 0;
            symbol_lookup[a][b][c] = true;
        }

        // Add to hashmap for O(1) lookup
        HashEntry* item = malloc(sizeof(HashEntry));
        item->key = strdup(entries[i].word);
        item->value = entries[i].symbol;
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
    for (size_t i = 0; i < len; i++) {
        used[(unsigned char)buffer[i]] = true;
    }
    for (int i = 0; i < 256; i++) {
        if (!used[i]) return (char)i;
    }
    fprintf(stderr, "All 256 characters are used. No escape character available.\n");
    exit(1);
}

void compress(const char* dict_path, const char* lang_path, const char* input_buffer, size_t input_len, int threads) {
    size_t dict_size = 0;
    HashEntry* hashmap = NULL;
    DictEntry* dict = load_dictionary(dict_path, lang_path, &dict_size, &hashmap, 'c');
    char escape_char = find_unused_char_from_buffer(input_buffer, input_len);

    FILE* out = fopen("out", "wb");
    if (!out) {
        fprintf(stderr, "Failed to open output file\n");
        exit(1);
    }

    fputc(escape_char, out);

    char** segments = malloc(sizeof(char*) * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));

    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num();
        size_t start = (input_len * tid) / threads;
        size_t end = (input_len * (tid + 1)) / threads;

        while (start > 0 && input_buffer[start] != ' ') start++;
        while (end < input_len && input_buffer[end] != ' ') end++;

        char* buffer = malloc((end - start) * 2);
        size_t out_pos = 0;

        size_t i = start;
        while (i < end) {
            size_t s = i;
            while (i < end && input_buffer[i] != ' ') i++;
            size_t len = i - s;

            if (len > 0) {
                char backup = input_buffer[i];
                ((char*)input_buffer)[i] = '\0';
                HashEntry* found = NULL;
                HASH_FIND_STR(hashmap, &input_buffer[s], found);
                ((char*)input_buffer)[i] = backup;

                if (found) {
                    size_t slen = strlen(found->value);
                    memcpy(&buffer[out_pos], found->value, slen);
                    out_pos += slen;
                } else if (is_symbol_fast(&input_buffer[s], len)) {
                    buffer[out_pos++] = escape_char;
                    memcpy(&buffer[out_pos], &input_buffer[s], len);
                    out_pos += len;
                } else {
                    memcpy(&buffer[out_pos], &input_buffer[s], len);
                    out_pos += len;
                }
            }

            if (i < end && input_buffer[i] == ' ') {
                buffer[out_pos++] = ' ';
                i++;
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

void decompress(const char* dict_path, const char* lang_path, const char* input_buffer, size_t input_len, int threads) {
    size_t dict_size = 0;
    HashEntry* hashmap = NULL;
    DictEntry* dict = load_dictionary(lang_path, dict_path, &dict_size, &hashmap, 'd');

    char escape_char = input_buffer[0];
    const char* data = input_buffer + 1;
    size_t data_len = input_len - 1;

    FILE* out = fopen("out_decompressed", "wb");
    if (!out) {
        fprintf(stderr, "Failed to open decompressed output file\n");
        exit(1);
    }

    char** segments = malloc(sizeof(char*) * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));

    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num();
        size_t start = (data_len * tid) / threads;
        size_t end = (data_len * (tid + 1)) / threads;

        // Align to word boundary
        while (start > 0 && data[start] != ' ') start++;
        while (end < data_len && data[end] != ' ') end++;

        char* buffer = malloc((end - start) * 8); // generous allocation for word expansion
        size_t out_pos = 0;

        size_t i = start;
        while (i < end) {
            bool is_escaped = (data[i] == escape_char);
            if (is_escaped) i++;

            size_t s = i;
            while (i < end && data[i] != ' ') i++;
            size_t len = i - s;

            if (len > 0) {
                char backup = data[i];
                ((char*)data)[i] = '\0';
                if (!is_escaped) {
                    HashEntry* found = NULL;
                    HASH_FIND_STR(hashmap, &data[s], found);
                    if (found) {
                        size_t word_len = strlen(found->key);
                        memcpy(&buffer[out_pos], found->key, word_len);
                        out_pos += word_len;
                    } else {
                        memcpy(&buffer[out_pos], &data[s], len);
                        out_pos += len;
                    }
                } else {
                    memcpy(&buffer[out_pos], &data[s], len);
                    out_pos += len;
                }
                ((char*)data)[i] = backup;
            }

            if (i < end && data[i] == ' ') {
                buffer[out_pos++] = ' ';
                i++;
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

char* read_file(const char* path, const char* label, size_t* out_len) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open %s file: %s\n", label, path);
        exit(1);
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    char* buffer = malloc(length);
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed for %s\n", label);
        fclose(file);
        exit(1);
    }
    size_t read_bytes = fread(buffer, 1, length, file);
    fclose(file);
    if (out_len) *out_len = read_bytes;
    return buffer;
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
    char* input_buffer = read_file(file_path, "Input file", &input_len);

    if (strcmp(mode_flag, "-c") == 0) {
        compress(dict_path, language_path, input_buffer, input_len, threads);
    } else if (strcmp(mode_flag, "-d") == 0) {
        decompress(dict_path, language_path, input_buffer, input_len, threads);
    } else {
        fprintf(stderr, "Invalid mode: %s. Use -c for compression or -d for decompression.\n", mode_flag);
        return 1;
    }

    free(input_buffer);
    return 0;
}
