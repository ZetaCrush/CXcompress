#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void read_file(const char* path, const char* label) {
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
    free(buffer);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <file_path> <dictionary_file_path> <integer_arg>\n", argv[0]);
        return 1;
    }

    const char* file_path = argv[1];
    const char* dict_path = argv[2];
    const char* language_path = argv[3];

    read_file(file_path, "Input file");
    read_file(dict_path, "Dictionary");
    read_file(language_path, "Language pack");

    return 0;
}

