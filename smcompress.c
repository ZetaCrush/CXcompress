#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include "uthash.h"

#define MAX_DICT_SIZE 50000
#define SEP ",.;?!\n-"
#define M 252

char* read_file_to_string(const char *filename) {
    FILE *file = fopen(filename, "rb");

    if (!file) {
        perror("Error opening file");
        return NULL;
    }

    size_t chunk_size = 4096;
    size_t total_read = 0;
    char *buffer = NULL;
    char *temp;

    while (1) {
        temp = realloc(buffer, total_read + chunk_size);
        if (!temp) {
            perror("Memory allocation failed");
            free(buffer);
            fclose(file);
            return buffer;
        }
        buffer = temp;

        size_t bytes_read = fread(buffer + total_read, 1, chunk_size, file);
        total_read += bytes_read;

        if (bytes_read < chunk_size) {
            if (ferror(file)) {
                perror("Error reading file");
                free(buffer);
                fclose(file);
                return buffer;
            }
            break;
        }
    }

    temp = realloc(buffer, total_read + 1);
    if (!temp) {
        free(buffer);
        fclose(file);
        return NULL;
    }
    buffer = temp;
    buffer[total_read] = '\0';

    fclose(file);
    return buffer;
}


bool is_sep(char c) {
    return strchr(SEP, c) != NULL;
}

char **split(char *s, char sep, size_t *out_count) {
    char **tokens = malloc(sizeof(char*) * 100000); // overallocate
    if (!tokens) {
        fprintf(stderr, "split(): failed to allocate tokens array\n");
        exit(1);
    }
    size_t count = 0;
    char delim[2] = {sep, '\0'};
    char *token = strtok(s, delim);
    while (token) {
        tokens[count++] = token;
        token = strtok(NULL, delim);
    }
    *out_count = count;
    return tokens;
}

void find_unused_chars(const char *text, char *C0, char *C1) {
    bool used[256] = { false };
    for (const char *p = text; *p; ++p) {
        used[(unsigned char)(*p)] = true;
    }
    for (int i = 1; i < 256; ++i) {
        if (!used[i] && i != '0' && i != '1') {
            *C0 = (char)i;
            for (int j = i + 1; j < 256; ++j) {
                if (!used[j] && j != '0' && j != '1') {
                    *C1 = (char)j;
                    return;
                }
            }
        }
    }
    fprintf(stderr, "Could not find two unused characters\n");
    exit(1);
}

void compress(const char *input_file, const char *output_file, const char *dict_file) {
    char* content_raw = read_file_to_string(input_file);
    char* dict_raw_original = read_file_to_string(dict_file ? dict_file : "dict");

    if (!content_raw || !dict_raw_original) {
        fprintf(stderr, "Error reading input or dictionary file\n");
        exit(1);
    }

    char *content = strdup(content_raw);
    char *dict_raw = strdup(dict_raw_original);
    if (!content || !dict_raw) {
        fprintf(stderr, "strdup failed\n");
        exit(1);
    }

    size_t dict_count;
    char **dict = split(dict_raw, '\n', &dict_count);

    int freq[256] = {0};
    for (char *p = content_raw; *p; ++p) freq[(unsigned char)*p]++;
    char mc = ' ';
    for (int i = 0; i < 256; ++i) {
        if (freq[i] > freq[(unsigned char)mc]) mc = (char)i;
    }

    char C0 = 0, C1 = 0;
    find_unused_chars(content_raw, &C0, &C1);

    char symbols[MAX_DICT_SIZE][2];

    size_t word_count;
    char **words = split(content, mc, &word_count);

    size_t out_size = 0, out_cap = 4096;
    char *output = malloc(out_cap);
    if (!output) {
        fprintf(stderr, "Failed to allocate output buffer\n");
        exit(1);
    }
    output[0] = '\0';

    size_t final_len = strlen(output);
    char *final_string = malloc(final_len + 4);
    if (!final_string) {
        fprintf(stderr, "Failed to allocate final string\n");
        exit(1);
    }
    snprintf(final_string, final_len + 4, "%c%c%c%s", C0, C1, mc, output);

    size_t final_size = strlen(final_string);
    size_t max_comp = ZSTD_compressBound(final_size);
    void* compressed = malloc(max_comp);
    if (!compressed) {
        fprintf(stderr, "Failed to allocate compressed buffer\n");
        exit(1);
    }
    size_t compressed_size = ZSTD_compress(compressed, max_comp, final_string, final_size, 3);

    if (ZSTD_isError(compressed_size)) {
        fprintf(stderr, "Compression failed: %s\n", ZSTD_getErrorName(compressed_size));
        exit(1);
    }

    FILE *out = fopen(output_file, "wb");
    if (!out) {
        perror("fopen output file");
        exit(1);
    }
    fwrite(compressed, 1, compressed_size, out);
    fclose(out);

    free(compressed);
    free(content_raw);
    free(content);
    free(dict_raw_original);
    free(dict_raw);
    free(output);
    free(final_string);
    free(dict);
    free(words);
}


void decompress(const char *input_file, const char *output_file, const char *dict_file) {
    char* content=read_file_to_string(input_file);
    char* dict=NULL;
    if (dict_file == NULL) {
        dict=read_file_to_string("dict");
    } else {
        dict=read_file_to_string(dict_file);
    }
}

const char *dictionary_file = NULL;

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 6) {
        fprintf(stderr, "Usage: %s [-c|-d] <input_file> <output_file> [-dict dictionary_file]\n", argv[0]);
        return 1;
    }

    const char *flag = argv[1];
    const char *input_file = argv[2];
    const char *output_file = argv[3];

    if (argc == 6 && strcmp(argv[4], "-dict") == 0) {
        dictionary_file = argv[5];
        printf("Using dictionary: %s\n", dictionary_file);
    } else if (argc == 5 || (argc == 6 && strcmp(argv[4], "-dict") != 0)) {
        fprintf(stderr, "Error: Invalid usage of optional -dict argument.\n");
        fprintf(stderr, "Usage: %s [-c|-d] <input_file> <output_file> [-dict dictionary_file]\n", argv[0]);
        return 1;
    }

    if (strcmp(flag, "-c") == 0) {
        printf("Compressing: %s -> %s\n", input_file, output_file);
        compress(input_file, output_file, dictionary_file);
    } else if (strcmp(flag, "-d") == 0) {
        printf("Decompressing: %s -> %s\n", input_file, output_file);
        decompress(input_file, output_file, dictionary_file);
    } else {
        fprintf(stderr, "Error: Invalid flag. Use -c for compress or -d for decompress.\n");
        return 1;
    }

    return 0;
}

