#include <stdio.h>
#include "utils.h"
#include "utils.c"

void compress(const char *input_file, const char *output_file, const char *dict_file) {
    char* content_raw = read_file_to_string(input_file);
    char* dict_raw_original = read_file_to_string(dict_file ? dict_file : "dict");

    printf("read raw files \n");
    if (!content_raw || !dict_raw_original) {
        printf("Error reading input or dictionary file\n");
        exit(1);
    }

    // content_raw[50000] = 0;

    char *content = strdup(content_raw);
    char *dict_raw = strdup(dict_raw_original);
    if (!content || !dict_raw) {
        printf("strdup failed\n");
        exit(1);
    }

    int freq[256] = {0};
    for (char *p = content_raw; *p; ++p) freq[(unsigned char)*p]++;
    char mc = ' ';
    for (int i = 0; i < 256; ++i) {
        if (freq[i] > freq[(unsigned char)mc]) mc = (char)i;
    }
    // Unused Char
    char C0 = 0, C1 = 0;
    find_unused_chars(content_raw, &C0, &C1);

    // Load Dictionary
    size_t dict_count;
    char **dict = split_lines(dict_raw, &dict_count);

    // Top Chars
    char top_chars[TOP_N];
    get_most_common_chars(content_raw, top_chars, 1); // exclude space

    for (int i = 0; i < dict_count && i < MAX_DICT_SIZE; ++i) {
        add_to_dict_set(dict[i]);
    }
    // Symbols
    char *symbols[MAX_DICT_SIZE];
    generate_symbols(top_chars, symbols, dict_count);

    printf("dict_count: %d \n", dict_count);

    // map vocab to symbols
    for (int i = 0; i < dict_count && i < MAX_DICT_SIZE; ++i) {
        add_mapping(dict[i], symbols[i]);
    }

    // File Content

    char *replaced = process_words(content, C0, C1, top_chars);
    printf("replaced, size=%d\n", strlen(replaced));

    // Final: compress
    compress_with_zstd(replaced, output_file);
    printf("✅ Compression complete. Output: %s\n", output_file);

    // Cleanup
    // free(replaced);
    // for (int i = 0; i < dict_count; ++i) free(dict[i]);
    // for (int i = 0; i < MAX_DICT_SIZE; ++i) free(symbols[i]);
}


void decompress(const char *input_file, const char *output_file, const char *dict_file) {
    char* content_raw = read_file_to_string(input_file);
    char* dict_raw_original = read_file_to_string(dict_file ? dict_file : "dict");

    if (!content_raw || !dict_raw_original) {
        printf("Error reading input or dictionary file\n");
        exit(1);
    }


    char *dict_raw = strdup(dict_raw_original);
    char *content = strdup(content_raw);

    size_t dict_count = 0;    
    char **dict = split_lines(dict_raw, &dict_count);
    
    size_t decompressed_size;
    char *decompressed_data = decompress_zstd(input_file, &decompressed_size);
    if (!decompressed_data) return;

    char *original_text = decode_symbols(decompressed_data, dict, dict_count);
    size_t original_text_size = strlen(original_text);

    printf("original text size: %d\n", original_text_size);
    FILE *fp = fopen(output_file, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open output file: %s\n", original_text);
        free(decompressed_data);
        free(original_text);
        return;
    }

    size_t written = fwrite(original_text, 1, original_text_size, fp);
    fclose(fp);
    free(decompressed_data);
    free(original_text);
    return;
}

const char *dictionary_file = NULL;

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 6) {
        printf("Usage: %s [-c|-d] <input_file> <output_file> [-dict dictionary_file]\n", argv[0]);
        return 1;
    }

    const char *flag = argv[1];
    const char *input_file = argv[2];
    const char *output_file = argv[3];

    if (argc == 6 && strcmp(argv[4], "-dict") == 0) {
        dictionary_file = argv[5];
        printf("Using dictionary: %s\n", dictionary_file);
    } else if (argc == 5 || (argc == 6 && strcmp(argv[4], "-dict") != 0)) {
        printf("Error: Invalid usage of optional -dict argument.\n");
        printf("Usage: %s [-c|-d] <input_file> <output_file> [-dict dictionary_file]\n", argv[0]);
        return 1;
    }

    if (strcmp(flag, "-c") == 0) {
        printf("Compressing: %s -> %s\n", input_file, output_file);
        compress(input_file, output_file, dictionary_file);
    } else if (strcmp(flag, "-d") == 0) {
        printf("Decompressing: %s -> %s\n", input_file, output_file);
        decompress(input_file, output_file, dictionary_file);
    } else {
        printf("Error: Invalid flag. Use -c for compress or -d for decompress.\n");
        return 1;
    }

    return 0;
}

