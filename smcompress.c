#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

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

void compress(const char *input_file, const char *output_file, const char *dict_file) {
    char* content=read_file_to_string(input_file);
    char* dict=NULL;
    if (dict_file == NULL) {
        dict=read_file_to_string("dict");
    } else {
        dict=read_file_to_string(dict_file);
    }
}

void decompress(const char *input_file, const char *output_file, const char *dict_file) {
    char* content=read_file_to_string(input_file);
    char* dict=NULL;
    if (dict_file == NULL) {
        dict=read_file_to_string("dict");
    } else {
        dict=read_file_to_string(dict_file);
    }
    fprintf("", "%s", content);
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

