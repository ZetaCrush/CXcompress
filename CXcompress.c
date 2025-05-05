#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_LINE 1024
#define MAX_ENTRIES 100000

typedef struct {
    char* word;
    char* symbol;
} DictEntry;

DictEntry* load_dictionary(const char* dict_path, const char* lang_path, size_t* count) {
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
        // Remove newline
        dict_line[strcspn(dict_line, "\r\n")] = 0;
        lang_line[strcspn(lang_line, "\r\n")] = 0;

        entries[i].word = strdup(dict_line);
        entries[i].symbol = strdup(lang_line);
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

void compress(const char* dict_path, const char* lang_path, const char* input_buffer, size_t input_len) {
    printf("Running compression setup...\n");

    size_t dict_size = 0;
    DictEntry* dict = load_dictionary(dict_path, lang_path, &dict_size);

    printf("Loaded %zu dictionary entries.\n", dict_size);

    char escape_char = find_unused_char_from_buffer(input_buffer, input_len);

    free_dictionary(dict, dict_size);
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

    char* buffer = (char*)malloc(length + 1);
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed for %s\n", label);
        fclose(file);
        exit(1);
    }

    size_t read_bytes = fread(buffer, 1, length, file);
    buffer[read_bytes] = '\0';

    fclose(file);
    printf("%s read: %zu bytes\n", label, read_bytes);
    if (out_len) *out_len = read_bytes;
    return buffer;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <-c|-d> <file_path> <dictionary_file_path> <language_file_path>\n", argv[0]);
        return 1;
    }

    const char* mode_flag = argv[1];
    const char* file_path = argv[2];
    const char* dict_path = argv[3];
    const char* language_path = argv[4];

    if (strcmp(mode_flag, "-c") == 0) {
        printf("Mode: Compression\n");
        size_t input_len = 0;
        char* input_buffer = read_file(file_path, "Input file", &input_len);
        compress(dict_path, language_path, input_buffer, input_len);
        free(input_buffer);
    } else if (strcmp(mode_flag, "-d") == 0) {
        printf("Mode: Decompression (not yet implemented)\n");
        size_t input_len0 = 0;
        size_t input_len1 = 0;
        size_t input_len2 = 0;
        read_file(file_path, "Input file", &input_len0);
        read_file(dict_path, "Dictionary", &input_len1);
        read_file(language_path, "Language pack", &input_len2);
    } else {
        fprintf(stderr, "Invalid mode: %s. Use -c for compression or -d for decompression.\n", mode_flag);
        return 1;
    }

    return 0;
}

