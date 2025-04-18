#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

char* read_file_to_string(const char *filename) {
    FILE *file = fopen(filename, "rb");

    if (!file) {
        perror("Error opening file");
        return "";
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

    temp = realloc(buffer, total_read);
    if (temp || total_read == 0) {
        buffer = temp;
    }

    fclose(file);
    return buffer;
}

void compress(const char *input_file, const char *output_file) {
    char* content=read_file_to_string(input_file);
    char* dict=read_file_to_string("dict");
}

void decompress(const char *input_file, const char *output_file) {
    char* content=read_file_to_string(input_file);
    char* dict=read_file_to_string("dict");
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s [-c|-d] <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    const char *flag = argv[1];
    const char *input_file = argv[2];
    const char *output_file = argv[3];

    if (strcmp(flag, "-c") == 0) {
        printf("Compressing: %s -> %s\n", input_file, output_file);
        compress(input_file, output_file);
    } else if (strcmp(flag, "-d") == 0) {
        printf("Decompressing: %s -> %s\n", input_file, output_file);
        decompress(input_file, output_file);
    } else {
        fprintf(stderr, "Error: Invalid flag. Use -c for compress or -d for decompress.\n");
        fprintf(stderr, "Usage: %s [-c|-d] <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    return 0;
}
