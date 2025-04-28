#include "utils.h"

#include <pthread.h>

// #define THREAD_COUNT 4 // or however many you want
#define MAX_DICT_SIZE 50000
#define SEP ",.;?!\n"
#define M 252

#define TOP_N 35

// Helper macro for debugging
#define DEBUG_PRINT(fmt, ...) \
    do { if (debug) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

// Struct to hold character and its frequency
typedef struct {
    unsigned char ch;
    int freq;
} CharFreq;

// Symbol → Word
typedef struct {
    char symbol[16];         // Key: symbol
    char word[128];          // Value: original word
    UT_hash_handle hh;
} SymbolMap;

// Word → Symbol
typedef struct {
    char word[128];          // Key: word
    char symbol[16];         // Value: symbol
    UT_hash_handle hh;
} WordMap;

typedef struct {
    char key[64];
    UT_hash_handle hh;
} dict_entry;

typedef struct {
    const char *chunk_start;
    size_t chunk_len;
    char **new_words;
    size_t new_words_count;
    size_t new_words_capacity;
    char C0;
} ThreadData;

dict_entry *dict_set = NULL;
SymbolMap *symbol_to_word = NULL;
WordMap *word_to_symbol = NULL;

// Comparison function for qsort (descending by freq)
int cmp_freq(const void *a, const void *b) {
    CharFreq *fa = (CharFreq *)a;
    CharFreq *fb = (CharFreq *)b;
    return fb->freq - fa->freq; // descending
}

// Function to get top 35 most common characters
int get_most_common_chars(const char *content_raw, char *result, int exclude_space) {
    int freq[256] = {0};

    for (const char *p = content_raw; *p; ++p) {
        freq[(unsigned char)*p]++;
    }

    CharFreq freqs[256];
    for (int i = 0; i < 256; ++i) {
        freqs[i].ch = (unsigned char)i;
        freqs[i].freq = freq[i];
    }

    qsort(freqs, 256, sizeof(CharFreq), cmp_freq);

    int count = 0, index = 0;
    for (int i = 0; i < 256 && index < TOP_N; ++i) {
        if (exclude_space && freqs[i].ch == ' '){
            index++;
            continue;
        }
        index++;
        result[count++] = freqs[i].ch;
    }

    result[count] = 0;
    return count;
}
char* read_file_to_string(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        return NULL;
    }

    // Seek to end to get file size
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);

    if (length < 0) {
        perror("ftell failed");
        fclose(file);
        return NULL;
    }

    // Allocate buffer (plus null terminator)
    char *buffer = malloc(length + 1);
    if (!buffer) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // Read all at once
    size_t read_size = fread(buffer, 1, length, file);
    fclose(file);

    if (read_size != length) {
        perror("Error reading file");
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}



bool is_sep(char c) {
    return strchr(SEP, c) != NULL;
}

char **split_lines(char *input, size_t *out_count) {
    size_t capacity = 100000;  // Start large enough
    char **tokens = malloc(sizeof(char*) * capacity);
    if (!tokens) {
        fprintf(stderr, "split_lines_inplace(): failed to allocate token array\n");
        exit(1);
    }

    size_t count = 0;
    char *p = input;

    while (*p) {
        // Skip leading newlines and carriage returns
        while (*p == '\n' || *p == '\r') ++p;
        if (*p == '\0') break;

        if (count >= capacity) {
            capacity *= 2;
            tokens = realloc(tokens, sizeof(char*) * capacity);
            if (!tokens) {
                fprintf(stderr, "split_lines_inplace(): failed to realloc token array\n");
                exit(1);
            }
        }

        tokens[count++] = p;

        // Find end of line and null-terminate it
        while (*p && *p != '\n' && *p != '\r') ++p;
        if (*p == '\n' || *p == '\r') *p++ = '\0'; // Null-terminate the word
    }

    *out_count = count;
    return tokens;
}


char **split(char *s, char sep, size_t *out_count) {
    size_t capacity = 2000000;  // Start small and grow as needed
    char **tokens = malloc(sizeof(char*) * capacity);
    if (!tokens) {
        printf("split(): failed to allocate tokens array\n");
        exit(1);
    }
    size_t count = 0;
    char delim[2] = {sep, '\0'};
    char *token = strtok(s, delim);
    while (token) {
        tokens[count] = malloc(strlen(token) + 1);
        if (!tokens[count]) {
            printf("split(): failed to allocate memory for token\n");
            exit(1);
        }
        strcpy(tokens[count], token);
        count++;
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
    printf("Could not find two unused characters\n");
    exit(1);
}


void add_to_dict_set(char *word) {
    dict_entry *entry = malloc(sizeof(dict_entry));
    strcpy(entry->key, word);
    HASH_ADD_STR(dict_set, key, entry);
}

int dict_contains(const char *word) {
    dict_entry *entry;
    HASH_FIND_STR(dict_set, word, entry);
    return entry != NULL;
}
// === Generate symbols ===
int symbol_exists_in_dict(const char *symbol, char **dict, int dict_count) {
    for (int i = 0; i < dict_count; ++i) {
        if (strcmp(dict[i], symbol) == 0) {
            return 1;
        }
    }
    return 0;
}

int generate_symbols(char top_chars[TOP_N], char *symbols[], int top_char_count) {
    int index = 0;
    char temp[4];

    // 1-char
    for (int i = 0; i < top_char_count; ++i) {
        temp[0] = top_chars[i]; temp[1] = '\0';
        symbols[index++] = strdup(temp);
    }

    // 2-char
    for (int i = 0; i < top_char_count; ++i) {
        for (int j = 0; j < top_char_count; ++j) {
            if (is_sep(top_chars[j])) continue;
            temp[0] = top_chars[i]; temp[1] = top_chars[j]; temp[2] = '\0';
            symbols[index++] = strdup(temp);
        }
    }

    // 3-char
    for (int i = 0; i < top_char_count; ++i) {
        for (int j = 0; j < top_char_count; ++j) {
            for (int k = 0; k < top_char_count; ++k) {
                if (is_sep(top_chars[k])) continue;
                temp[0] = top_chars[i]; temp[1] = top_chars[j]; temp[2] = top_chars[k]; temp[3] = '\0';
                symbols[index++] = strdup(temp);
            }
        }
    }

    return index;
}



// Debug flag
int debug = 1;

void add_mapping(const char *word, const char *symbol) {
    // Debug: Start of the function
    // DEBUG_PRINT("Adding mapping for word: '%s', symbol: '%s'\n", word, symbol);

    // Allocate memory for SymbolMap
    SymbolMap *sentry = malloc(sizeof(SymbolMap));
    if (!sentry) {
        fprintf(stderr, "Failed to allocate memory for SymbolMap.\n");
        exit(1);
    }
    // DEBUG_PRINT("Allocated memory for SymbolMap %c\n", ' ');

    // Copy symbol and word into the struct
    strncpy(sentry->symbol, symbol, sizeof(symbol));
    strncpy(sentry->word, word, sizeof(word));

    // Debug: Print symbol and word sizes after copying
    // DEBUG_PRINT("Copied symbol: '%s', word: '%s'\n", sentry->symbol, sentry->word);

    // Add to symbol-to-word hash table
    HASH_ADD_STR(symbol_to_word, symbol, sentry);
    // DEBUG_PRINT("Added to symbol_to_word hash table %c\n", ' ');

    // Allocate memory for WordMap
    WordMap *wentry = malloc(sizeof(WordMap));
    if (!wentry) {
        fprintf(stderr, "Failed to allocate memory for WordMap.\n");
        exit(1);
    }
    // DEBUG_PRINT("Allocated memory for WordMap %c\n", ' ');

    // Copy word and symbol into the struct
    strncpy(wentry->word, word, sizeof(word));
    strncpy(wentry->symbol, symbol, sizeof(symbol));

    // Debug: Print word and symbol sizes after copying
    // DEBUG_PRINT("Copied word: '%s', symbol: '%s'\n", wentry->word, wentry->symbol);

    // Add to word-to-symbol hash table
    HASH_ADD_STR(word_to_symbol, word, wentry);
    // DEBUG_PRINT("Added to word_to_symbol hash table %c\n", ' ');
}

const char* get_word_by_symbol(const char *symbol) {
    SymbolMap *s;
    HASH_FIND_STR(symbol_to_word, symbol, s);
    return s ? s->word : NULL;
}

const char* get_symbol_by_word(const char *word) {
    WordMap *w;
    HASH_FIND_STR(word_to_symbol, word, w);
    return w ? w->symbol : NULL;
}

void free_maps() {
    SymbolMap *s, *tmp_s;
    HASH_ITER(hh, symbol_to_word, s, tmp_s) {
        HASH_DEL(symbol_to_word, s);
        free(s);
    }

    WordMap *w, *tmp_w;
    HASH_ITER(hh, word_to_symbol, w, tmp_w) {
        HASH_DEL(word_to_symbol, w);
        free(w);
    }
}


// === Replace words with symbols ===
static inline int append_to_string_array(char ***array, size_t *count, size_t *capacity, char *new_str, int space_n) {
    if (__builtin_expect(*count >= *capacity, 0)) {
        *capacity *= 2;
        char **temp = realloc(*array, (*capacity) * sizeof(char *));
        if (!temp) return 0;
        *array = temp;
    }
    size_t len = strlen(new_str);
    char *copy = malloc(len + space_n + 1);
    if (!copy) return 0;

    memcpy(copy, new_str, len);
    memset(copy + len, ' ', space_n);
    copy[len + space_n] = '\0';

    (*array)[(*count)++] = copy;
    // if(space_n > 2)
    //     printf("%s|\n", copy);
    return 1;
}

void *process_chunk(void *arg) {
    ThreadData *data = (ThreadData *)arg;

    const char *s = data->chunk_start;
    char C0 = data->C0;
    size_t len = data->chunk_len;
    size_t new_words_capacity = 1000;
    char **new_words = malloc(new_words_capacity * sizeof(char*));
    if (!new_words) return NULL;

    size_t new_words_count = 0;
    char word[128];
    size_t word_len = 0;

    for (size_t i = 0; i <= len; ++i) {
        char c = s[i];

        if (c == ' ' || c == '\0') {
            word[word_len] = '\0';
            int space_n = 0;
            while (s[i+1] == ' ')
            {
                space_n++;
                i++;
                /* code */
            }
            
            const char *sym = get_symbol_by_word(word);

            if (sym) {
                append_to_string_array(&new_words, &new_words_count, &new_words_capacity, strdup(sym), space_n);
                // new_words[new_words_count++] = strdup(sym);
            } else if (word_len > 1 && is_sep(word[word_len - 1])) {
                char base[128];
                strncpy(base, word, word_len - 1);
                base[word_len - 1] = '\0';
                const char *sym2 = get_symbol_by_word(base);
                if (sym2) { // word[:-1] in d and word[-1] in SEP:
                    char *combined = malloc(strlen(sym2) + 2);
                    sprintf(combined, "%s%c", sym2, word[word_len - 1]);
                    append_to_string_array(&new_words, &new_words_count, &new_words_capacity, combined, space_n);
                    // new_words[new_words_count++] = combined;
                } else {
                    const char *gfound = get_word_by_symbol(base);
                    if (gfound) { // word[:-1] in g and word[-1] in SEP:
                        char *marked = malloc(strlen(word) + 2);
                        sprintf(marked, "%s%c", word, C0);
                        // if(space_n > 3)
                        //     printf("%s %s| %d\n", word, marked, space_n);
                        append_to_string_array(&new_words, &new_words_count, &new_words_capacity, marked, space_n);
                        // new_words[new_words_count++] = marked;
                    } else { // else: # Default case, word is not common
                        append_to_string_array(&new_words, &new_words_count, &new_words_capacity, strdup(word), space_n);
                        // new_words[new_words_count++] = strdup(word);
                    }
                }
            } else {
                const char *gfound = get_word_by_symbol(word);
                if (gfound) { // elif word in g: # Word is a used symbol, add a marker
                    char *marked = malloc(strlen(word) + 2);
                    sprintf(marked, "%c%s", C0, word);
                    append_to_string_array(&new_words, &new_words_count, &new_words_capacity, marked, space_n);
                    // new_words[new_words_count++] = marked;
                } else { // else: # Default case, word is not common
                    append_to_string_array(&new_words, &new_words_count, &new_words_capacity, strdup(word), space_n);
                    // new_words[new_words_count++] = strdup(word);
                }
            }

            word_len = 0;
            if (c == '\0'){
                // printf(" End of FILE ! ");
                break;
            }
        } else {
            word[word_len++] = c;
        }
    }
    data->new_words = new_words;
    data->new_words_count = new_words_count;
    data->new_words_capacity = new_words_capacity;

    return NULL;
}

// Main function to process the input string and return as a single char* string
char* process_words(const char *s, char C0, char C1, const char *one_char_symbols, size_t top_char_count, size_t thread_count) {
    // size_t new_words_capacity = 1000;
    // char **new_words = malloc(new_words_capacity * sizeof(char*));
    // if (!new_words) return NULL;

    // size_t new_words_count = 0;
    // char word[128];
    // size_t word_len = 0;

    // for (size_t i = 0;; ++i) {
    //     char c = s[i];

    //     if (c == ' ' || c == '\0') {
    //         word[word_len] = '\0';
    //         int space_n = 0;
    //         while (s[i+1] == ' ')
    //         {
    //             space_n++;
    //             i++;
    //             /* code */
    //         }
            
    //         const char *sym = get_symbol_by_word(word);

    //         if (sym) {
    //             append_to_string_array(&new_words, &new_words_count, &new_words_capacity, strdup(sym), space_n);
    //             // new_words[new_words_count++] = strdup(sym);
    //         } else if (word_len > 1 && is_sep(word[word_len - 1])) {
    //             char base[128];
    //             strncpy(base, word, word_len - 1);
    //             base[word_len - 1] = '\0';
    //             const char *sym2 = get_symbol_by_word(base);
    //             if (sym2) {
    //                 char *combined = malloc(strlen(sym2) + 2);
    //                 sprintf(combined, "%s%c", sym2, word[word_len - 1]);
    //                 append_to_string_array(&new_words, &new_words_count, &new_words_capacity, combined, space_n);
    //                 // new_words[new_words_count++] = combined;
    //             } else {
    //                 const char *gfound = get_word_by_symbol(base);
    //                 if (gfound) {
    //                     char *marked = malloc(strlen(word) + 2);
    //                     sprintf(marked, "%s%c", word, C0);
    //                     // if(space_n > 3)
    //                     //     printf("%s %s| %d\n", word, marked, space_n);
    //                     append_to_string_array(&new_words, &new_words_count, &new_words_capacity, marked, space_n);
    //                     // new_words[new_words_count++] = marked;
    //                 } else {
    //                     append_to_string_array(&new_words, &new_words_count, &new_words_capacity, strdup(word), space_n);
    //                     // new_words[new_words_count++] = strdup(word);
    //                 }
    //             }
    //         } else {
    //             const char *gfound = get_word_by_symbol(word);
    //             if (gfound) {
    //                 char *marked = malloc(strlen(word) + 2);
    //                 sprintf(marked, "%c%s", C0, word);
    //                 append_to_string_array(&new_words, &new_words_count, &new_words_capacity, marked, space_n);
    //                 // new_words[new_words_count++] = marked;
    //             } else {
    //                 append_to_string_array(&new_words, &new_words_count, &new_words_capacity, strdup(word), space_n);
    //                 // new_words[new_words_count++] = strdup(word);
    //             }
    //         }

    //         word_len = 0;
    //         if (c == '\0'){
    //             printf(" End of FILE ! ");
    //             break;
    //         }
    //     } else {
    //         word[word_len++] = c;
    //     }
    // }
    // printf("new_words_count = %d \n", new_words_count);

    pthread_t threads[thread_count];
    ThreadData thread_data[thread_count];

    size_t len = strlen(s);
    size_t chunk_size = len / thread_count;

    size_t last_end = 0;

    printf("chunk_size=%d\n", chunk_size);
    for (int t = 0; t < thread_count; ++t) {
        size_t start = last_end;
        size_t end = (t == thread_count - 1) ? len : (t + 1) * chunk_size;

        // IMPORTANT: Adjust the end to the next space, so we don't split words in half
        while (end < len && s[end] != ' ' && s[end] != '\0') {
            end++;
        }

        if(s[end] == ' ') // IMPORTANT: should leave a space a EOF at the end of each chunk
            end++;

        last_end = end;

        thread_data[t].chunk_start = s + start;
        thread_data[t].chunk_len = end - start;
        thread_data[t].C0 = C0;

        pthread_create(&threads[t], NULL, process_chunk, &thread_data[t]);
    }

    for (int t = 0; t < thread_count; ++t) {
        pthread_join(threads[t], NULL);
    }

    // Merge results
    size_t new_words_count = 0;
    for (int t = 0; t < thread_count; ++t) {
        new_words_count += thread_data[t].new_words_count;
    }
    new_words_count++;
    char **new_words = malloc(new_words_count * sizeof(char*)); // adjust size as needed
    new_words_count = 0; // re-initialize for indexing

    for (int t = 0; t < thread_count; ++t) {
        for (size_t i = 0; i < thread_data[t].new_words_count; ++i) {
            new_words[new_words_count++] = thread_data[t].new_words[i];
        }
        free(thread_data[t].new_words);
    }

    // NULL-terminate if needed
    new_words[new_words_count] = NULL;

    // printf("new_words_count: %d\n", new_words_count);

    // Build final result
    size_t result_len = 3 + strlen(one_char_symbols); // C0, C1, and symbols + 1 C1
    for (size_t k = 0; k < new_words_count; ++k) {
        result_len += strlen(new_words[k]) + 1;  // spaces or C1
    }

    char *result = malloc(result_len + 1);
    if (!result) {
        for (size_t i = 0; i < new_words_count; ++i) free(new_words[i]);
        free(new_words);
        return NULL;
    }

    char *p = result;
    *p++ = C0;
    char *nr = malloc(2); // represent length one_char symbols in 1 byte
    sprintf(nr, "%c", top_char_count);
    strcpy(p, nr);
    p++;
    *p++ = C1;
    strcpy(p, one_char_symbols);
    p += strlen(one_char_symbols);
    *p++ = C1;

    for (size_t i = 0; i < new_words_count; ++i) {
        size_t len = strlen(new_words[i]);
        memcpy(p, new_words[i], len);
        p += len;
        if (i < new_words_count - 1) *p++ = ' ';
        free(new_words[i]);
    }
    *p = '\0';

    free(new_words);
    return result;
}

// === Compress with Zstd ===

int compress_with_zstd(const char *input, const char *out_file) {
    size_t input_size = strlen(input);
    size_t max_compressed_size = ZSTD_compressBound(input_size);

    void *compressed_data = malloc(max_compressed_size);
    if (!compressed_data) {
        fprintf(stderr, "Memory allocation failed.\n");
        return -1;
    }

    size_t compressed_size = ZSTD_compress(
        compressed_data, max_compressed_size, input, input_size, 1  // Compression level 1
    );

    if (ZSTD_isError(compressed_size)) {
        fprintf(stderr, "ZSTD compression error: %s\n", ZSTD_getErrorName(compressed_size));
        free(compressed_data);
        return -1;
    }

    FILE *fp = fopen(out_file, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open output file: %s\n", out_file);
        free(compressed_data);
        return -1;
    }

    size_t written = fwrite(compressed_data, 1, compressed_size, fp);
    fclose(fp);
    free(compressed_data);

    if (written != compressed_size) {
        fprintf(stderr, "Failed to write all compressed data to file.\n");
        return -1;
    }

    return 0;  // Success
}

// === Step 1: Decompress with Zstd ===
char *decompress_zstd(const char *filename, size_t *out_size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open compressed file");
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    size_t compressed_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    void *compressed_data = malloc(compressed_size);
    fread(compressed_data, 1, compressed_size, fp);
    fclose(fp);

    unsigned long long const rSize = ZSTD_getFrameContentSize(compressed_data, compressed_size);
    if (rSize == ZSTD_CONTENTSIZE_ERROR || rSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        fprintf(stderr, "Unable to determine decompressed size\n");
        free(compressed_data);
        return NULL;
    }

    char *decompressed_data = malloc(rSize + 1);
    if (!decompressed_data) {
        free(compressed_data);
        return NULL;
    }

    size_t const dSize = ZSTD_decompress(decompressed_data, rSize, compressed_data, compressed_size);
    if (ZSTD_isError(dSize)) {
        fprintf(stderr, "Decompression error: %s\n", ZSTD_getErrorName(dSize));
        free(decompressed_data);
        free(compressed_data);
        return NULL;
    }

    decompressed_data[dSize] = '\0';
    *out_size = dSize;

    free(compressed_data);
    return decompressed_data;
}

// === Step 2-5: Decode & Reconstruct ===
char *decode_symbols(const char *compressed, char** dict, size_t dict_count) {
    const char C0 = compressed[0];
    const char nr = compressed[1];
    const char C1 = compressed[2];

    size_t top_char_count = nr;


    const char *ptr = compressed + 3;
    char one_char_symbols[top_char_count + 1];
    int i = 0;
    while (*ptr && *ptr != C1 && i < top_char_count) {
        one_char_symbols[i++] = *ptr++;
    }
    one_char_symbols[i] = '\0';
    ptr++;  // Skip the C1 separator after symbols
    
    const char *new_words_text = ptr;

    // for (int i = 0; i < dict_count && i < MAX_DICT_SIZE; ++i) {
    //     add_to_dict_set(dict[i]);
    // }
    // Generate all possible symbols (1, 2, 3-char combinations)
    char **symbols = malloc(sizeof(char*) * MAX_DICT_SIZE);
    int symbol_count = generate_symbols(one_char_symbols, symbols, top_char_count);

    printf("symbol_count: %d\n", symbol_count);
    // Populate dummy mapping for demonstration (this should be replaced with actual dictionary loading)
    for (int i = 0; i < dict_count && i < symbol_count; ++i) {
        add_mapping(dict[i], symbols[i]);
    }

    // Split new_words_text using C1 as delimiter
    const char *p = new_words_text;
    char *output = malloc(strlen(new_words_text) * 5); // Generous buffer
    char *out_ptr = output;

    while (*p) {
        // Handle spaces first
        if (*p == ' ') {
            *out_ptr++ = ' ';
            ++p;
            continue;
        }

        if (*p == C1){ // skip STX
            ++p;
            continue;
        }


        // Extract a token (word/symbol until next space or end)
        const char *start = p;
        while (*p && *p != ' ') p++;

        size_t len = p - start;
        char token[256] = {0}; // assuming no token >255 chars
        strncpy(token, start, len);
        token[len] = '\0';

        const char *original = get_word_by_symbol(token);  // symbol → word map
        
        if (original) {
            out_ptr += sprintf(out_ptr, "%s", original);
        } else {
            int tlen = strlen(token);
            if (tlen > 0 && token[0] == C0) {
                out_ptr += sprintf(out_ptr, "%s", token + 1);
            } else if (tlen > 1 && token[tlen - 1] == C0 && is_sep(token[tlen - 2])) {
                // if(token[tlen - 2] == '\n')
                //     printf("%s|\n", token);
                token[tlen - 1] = '\0';
                out_ptr += sprintf(out_ptr, "%s", token);
            } else if (tlen > 1 && is_sep(token[tlen - 1])) {
                char tmp = token[tlen - 1];
                token[tlen - 1] = '\0';
                const char *base = get_word_by_symbol(token);
                if (base) {
                    out_ptr += sprintf(out_ptr, "%s%c", base, tmp);
                } else {
                    token[tlen - 1] = tmp;
                    out_ptr += sprintf(out_ptr, "%s", token);
                }
            } else {
                out_ptr += sprintf(out_ptr, "%s", token);
            }
        }
    }
    *out_ptr = '\0'; // null-terminate

    return output;

}
