#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <omp.h>
#include <sys/mman.h> // For mmap
#include <sys/stat.h> // For fstat
#include <fcntl.h>    // For open
#include <unistd.h>   // For close, munmap

// Use khash instead of uthash for hash table
#define KHASH_INLINE
#include "khash.h"
KHASH_MAP_INIT_STR(strmap, char*) // Defines khash_t(strmap) and related functions

#define MAX_LINE 1024 // Maximum line length for dictionary/language files
#define MAX_ENTRIES 100000 // Placeholder, actual entries depend on file size

// Faster symbol lookup for 1-3 character symbols
// This 3D array allows O(1) lookup for symbols up to 3 characters long.
// The first char is the first dimension, second char the second, etc.
bool symbol_lookup[256][256][256] = {{{ false }}};

// Structure to represent a token's span in the input data
typedef struct {
    size_t start;    // Starting offset of the token in the input string
    size_t len;      // Length of the token
    bool is_space;   // True if the token is a space/delimiter, false otherwise
} TokenSpan;

// Arena allocator for efficient memory management of many small objects
typedef struct {
    char* buffer; // Pointer to the allocated memory block
    size_t size;  // Total size of the buffer
    size_t used;  // Amount of buffer currently used
} Arena;

// Initializes an arena with a given size
Arena create_arena(size_t size) {
    Arena a = { .buffer = malloc(size), .size = size, .used = 0 };
    if (!a.buffer) {
        perror("malloc"); // Print error if malloc fails
        exit(1);
    }
    return a;
}

// Allocates memory from the arena. Reallocates if not enough space.
void* arena_alloc(Arena* a, size_t size) {
    // Check if current buffer has enough space
    if (a->used + size > a->size) {
        // If not, double the buffer size or make it large enough for the current allocation
        size_t new_size = (a->used + size) * 2;
        char* new_buffer = realloc(a->buffer, new_size);
        if (!new_buffer) {
            perror("realloc"); // Print error if realloc fails
            exit(1);
        }
        a->buffer = new_buffer;
        a->size = new_size;
    }
    void* ptr = a->buffer + a->used; // Get pointer to the next available space
    a->used += size; // Update used space
    return ptr;
}

// Frees the memory allocated by the arena
void destroy_arena(Arena* a) {
    free(a->buffer);
    a->buffer = NULL;
    a->size = a->used = 0;
}

// Structure to hold information about a memory-mapped file
typedef struct {
    const char* data; // Pointer to the memory-mapped data
    size_t size;      // Size of the mapped file
    int fd;           // File descriptor (needed for unmapping)
} MappedFile;

// Maps a file into memory for efficient reading
MappedFile map_file(const char* path) {
    MappedFile mf = {NULL, 0, -1};
    int fd = open(path, O_RDONLY); // Open file in read-only mode
    if (fd == -1) {
        perror("Error opening file");
        exit(1);
    }

    struct stat st;
    if (fstat(fd, &st) == -1) { // Get file size
        perror("Error getting file size");
        close(fd);
        exit(1);
    }

    if (st.st_size == 0) {
        fprintf(stderr, "File is empty: %s\n", path);
        close(fd);
        return mf;
    }

    // Map the file into memory
    void* data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        perror("Error mapping file");
        close(fd);
        exit(1);
    }

    mf.data = (const char*)data;
    mf.size = st.st_size;
    mf.fd = fd; // Store fd for later munmap

    // Advise the kernel about expected access pattern for performance
    #ifdef __linux__
    madvise(data, st.st_size, MADV_SEQUENTIAL);
    #endif

    return mf;
}

// Unmaps a memory-mapped file and closes its file descriptor
void unmap_file(MappedFile mf) {
    if (mf.data != NULL) {
        munmap((void*)mf.data, mf.size); // Unmap the memory
    }
    if (mf.fd != -1) {
        close(mf.fd); // Close the file descriptor
    }
}

// Optimized tokenizer: identifies words and delimiters without modifying input
TokenSpan* tokenize(const char* input, size_t len, size_t* token_count, Arena* arena) {
    // Estimate max tokens (every char could be a token in worst case, but typically less)
    // Allocate enough space in the arena for the TokenSpan array
    TokenSpan* tokens = arena_alloc(arena, sizeof(TokenSpan) * (len + 1));
    size_t count = 0;

    const char* p = input;
    const char* end = input + len;
    // Define common delimiters. Can be extended.
    const char* delims = " ,.?!\n\r\t"; // Added tab

    while (p < end) {
        const char* start = p;
        // Check if the current character is a delimiter
        if (strchr(delims, *p)) {
            tokens[count++] = (TokenSpan){ .start = start - input, .len = 1, .is_space = true };
            p++; // Move to the next character
        } else {
            // If not a delimiter, it's the start of a word. Find the end of the word.
            while (p < end && !strchr(delims, *p)) {
                p++;
            }
            tokens[count++] = (TokenSpan){ .start = start - input, .len = p - start, .is_space = false };
        }
    }

    *token_count = count; // Return the total number of tokens found
    return tokens;
}

// Fast lookup for 1-3 character symbols using the precomputed symbol_lookup array
bool is_symbol_fast(const char* word, size_t len) {
    if (len == 0 || len > 3) return false; // Only check for 1-3 character words
    unsigned char a = word[0];
    unsigned char b = (len > 1) ? word[1] : 0; // Use 0 if not present
    unsigned char c = (len > 2) ? word[2] : 0; // Use 0 if not present
    return symbol_lookup[a][b][c];
}

// Structure to pass data to each compression/decompression thread
typedef struct {
    const char* input;          // Pointer to the original input data (memory-mapped)
    const TokenSpan* tokens;    // Array of token spans
    size_t token_start;         // Starting index of tokens for this thread
    size_t token_end;           // Ending index of tokens for this thread
    khash_t(strmap)* hashmap;   // Pointer to the shared hash map (read-only in threads)
    char escape_char;           // The chosen escape character
    char* output;               // Pre-allocated output buffer for this thread
    size_t output_len;          // Actual length of data written to output by this thread
} ThreadData;

// Core compression/decompression logic executed by each thread
void process_thread(ThreadData* data, bool is_compress) {
    size_t out_pos = 0;
    char temp[256]; // Stack-allocated buffer for temporary string operations

    for (size_t i = data->token_start; i < data->token_end; i++) {
        const TokenSpan tok = data->tokens[i];
        const char* ptr = data->input + tok.start;

        if (tok.is_space) {
            // If it's a space/delimiter, just copy it directly
            memcpy(data->output + out_pos, ptr, tok.len);
            out_pos += tok.len;
        } else {
            // If it's a word, prepare it for hashmap lookup
            size_t len_to_copy = tok.len > 255 ? 255 : tok.len; // Prevent buffer overflow
            memcpy(temp, ptr, len_to_copy);
            temp[len_to_copy] = '\0'; // Null-terminate for khash lookup

            khiter_t k = kh_get(strmap, data->hashmap, temp); // Look up in hashmap

            if (k != kh_end(data->hashmap)) {
                // If found in hashmap, copy its mapped value
                const char* val = kh_value(data->hashmap, k);
                size_t slen = strlen(val);
                memcpy(data->output + out_pos, val, slen);
                out_pos += slen;
            } else if (is_compress && is_symbol_fast(temp, len_to_copy)) {
                // If compressing and it's a known symbol but not in dict (e.g., a single char),
                // add escape character and then the original word.
                data->output[out_pos++] = data->escape_char;
                memcpy(data->output + out_pos, ptr, tok.len);
                out_pos += tok.len;
            } else {
                // If not found in hashmap and not a special case, copy original word
                memcpy(data->output + out_pos, ptr, tok.len);
                out_pos += tok.len;
            }
        }
    }
    data->output_len = out_pos; // Store the actual length written by this thread
}


// Loads dictionary and language files into the hashmap
void load_dictionary(const char* dict_path, const char* lang_path,
                     khash_t(strmap)* hashmap, char mode) {
    MappedFile dict_file = map_file(dict_path);
    if (dict_file.size == 0) {
        fprintf(stderr, "Dictionary file is empty or invalid: %s\n", dict_path);
        exit(1);
    }
    MappedFile lang_file = map_file(lang_path);
    if (lang_file.size == 0) {
        fprintf(stderr, "Language file is empty or invalid: %s\n", lang_path);
        exit(1);
    }

    const char* dict_ptr = dict_file.data;
    const char* lang_ptr = lang_file.data;
    const char* dict_end = dict_file.data + dict_file.size;
    const char* lang_end = lang_file.data + lang_file.size;

    char dict_buf[MAX_LINE];
    char lang_buf[MAX_LINE];

    while (dict_ptr < dict_end && lang_ptr < lang_end) {
        // Find end of current line in dictionary file
        const char* dict_line_end = (const char*)memchr(dict_ptr, '\n', dict_end - dict_ptr);
        // Find end of current line in language file
        const char* lang_line_end = (const char*)memchr(lang_ptr, '\n', lang_end - lang_ptr);

        // If either line end is not found, or we're at the end of a file, break
        if (!dict_line_end || !lang_line_end) {
            break;
        }

        size_t dict_len = dict_line_end - dict_ptr;
        size_t lang_len = lang_line_end - lang_ptr;

        // Check for excessively long lines
        if (dict_len >= MAX_LINE || lang_len >= MAX_LINE) {
            fprintf(stderr, "Line too long in dictionary or language file. Max allowed: %d\n", MAX_LINE - 1);
            exit(1);
        }

        // Copy line content to temporary buffers and null-terminate
        memcpy(dict_buf, dict_ptr, dict_len);
        dict_buf[dict_len] = '\0';
        memcpy(lang_buf, lang_ptr, lang_len);
        lang_buf[lang_len] = '\0';

        int ret; // Return value from kh_put (0 if key exists, 1 if new, 2 if updated)
        khiter_t k; // Iterator for hashmap

        if (mode == 'c') { // Compression mode: dict_buf -> lang_buf
            // Try to insert dict_buf as key
            k = kh_put(strmap, hashmap, strdup(dict_buf), &ret);
            if (ret == 0) { // Key already exists (ret == 0)
                // Free the newly duplicated key (dict_buf) as it's not used
                free((char*)kh_key(hashmap, k)); // Free the existing key
                kh_value(hashmap, k) = strdup(lang_buf); // Update value
            } else { // Key was new (ret == 1 or 2)
                kh_value(hashmap, k) = strdup(lang_buf); // Set the value
            }

            // Populate symbol_lookup for fast checks during compression
            if (lang_len > 0 && lang_len <= 3) {
                unsigned char a = (unsigned char)lang_buf[0];
                unsigned char b = (lang_len > 1) ? (unsigned char)lang_buf[1] : 0;
                unsigned char c = (lang_len > 2) ? (unsigned char)lang_buf[2] : 0;
                symbol_lookup[a][b][c] = true;
            }
        } else { // Decompression mode: lang_buf -> dict_buf
            // Try to insert lang_buf as key
            k = kh_put(strmap, hashmap, strdup(lang_buf), &ret);
            if (ret == 0) { // Key already exists (ret == 0)
                // Free the newly duplicated key (lang_buf) as it's not used
                free((char*)kh_key(hashmap, k)); // Free the existing key
                kh_value(hashmap, k) = strdup(dict_buf); // Update value
            } else { // Key was new (ret == 1 or 2)
                kh_value(hashmap, k) = strdup(dict_buf); // Set the value
            }
        }

        // Move pointers past the newline character to the start of the next line
        dict_ptr = dict_line_end + 1;
        lang_ptr = lang_line_end + 1;
    }

    unmap_file(dict_file);
    unmap_file(lang_file);
}

// Finds an unused character in the input data to use as an escape character
char find_unused_char(const char* data, size_t len) {
    bool used[256] = {0}; // Initialize all to false
    used[0] = true; // Null character is typically not used and reserved
    for (size_t i = 0; i < len; i++) {
        used[(unsigned char)data[i]] = true; // Mark characters present in input as used
    }
    for (int i = 1; i < 256; i++) { // Iterate from 1 to 255
        if (!used[i]) {
            return (char)i; // Return the first unused character found
        }
    }
    fprintf(stderr, "No escape character available in the ASCII range 1-255.\n");
    exit(1); // Exit if no suitable character is found
}

// Main compression function
void compress(const char* dict_path, const char* lang_path,
              const char* input, size_t input_len, int threads) {
    // Initialize hash table for dictionary lookups
    khash_t(strmap)* hashmap = kh_init(strmap);
    load_dictionary(dict_path, lang_path, hashmap, 'c'); // Load dictionary for compression

    // Find a unique escape character not present in the input file
    char escape_char = find_unused_char(input, input_len);

    // Tokenize the input file using an arena allocator
    Arena arena = create_arena(input_len / 4); // Initial arena size (can grow)
    size_t token_count;
    TokenSpan* tokens = tokenize(input, input_len, &token_count, &arena);

    // Open output file for writing (binary mode)
    FILE* out = fopen("out.compressed", "wb");
    if (!out) {
        perror("fopen");
        exit(1);
    }
    fputc(escape_char, out); // Write the chosen escape character at the beginning of the file

    // Allocate memory for thread-specific data and output buffers
    ThreadData* thread_data = malloc(sizeof(ThreadData) * threads);
    char** outputs = malloc(sizeof(char*) * threads);
    // Calculate tokens per thread for even distribution
    size_t tokens_per_thread = (token_count + threads - 1) / threads;

    // Parallel region using OpenMP
    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num(); // Get current thread ID
        size_t start = tid * tokens_per_thread; // Calculate start token index for this thread
        size_t end = (tid + 1) * tokens_per_thread; // Calculate end token index for this thread
        if (end > token_count) end = token_count; // Adjust end for the last thread

        // Allocate output buffer for this thread (worst case: input_len * 2 for escape chars)
        outputs[tid] = malloc(input_len * 2);
        if (!outputs[tid]) {
            fprintf(stderr, "Thread %d: Failed to allocate output buffer.\n", tid);
            exit(1);
        }

        // Populate ThreadData structure for this thread
        ThreadData data = {
            .input = input,
            .tokens = tokens,
            .token_start = start,
            .token_end = end,
            .hashmap = hashmap,
            .escape_char = escape_char,
            .output = outputs[tid]
        };

        process_thread(&data, true); // Execute compression logic for this thread
        thread_data[tid] = data; // Store the data (including output_len)
    }

    // Write output sequentially from each thread's buffer
    for (int i = 0; i < threads; i++) {
        fwrite(thread_data[i].output, 1, thread_data[i].output_len, out);
        free(outputs[i]); // Free thread's output buffer
    }

    // Cleanup
    fclose(out);
    free(outputs);
    free(thread_data);

    // Free hash table values and destroy the hash map
    khiter_t k;
    for (k = kh_begin(hashmap); k != kh_end(hashmap); ++k) {
        if (kh_exist(hashmap, k)) {
            free((char*)kh_key(hashmap, k)); // Free the key string
            free(kh_value(hashmap, k));      // Free the value string
        }
    }
    kh_destroy(strmap, hashmap);
    destroy_arena(&arena); // Destroy the arena allocator
}

// Main decompression function
void decompress(const char* dict_path, const char* lang_path,
                const char* input, size_t input_len, int threads) {
    if (input_len < 1) return; // Handle empty input

    // First byte is the escape character
    char escape_char = input[0];
    const char* data_input = input + 1; // Actual data starts after the escape char
    size_t data_len = input_len - 1;

    // Initialize hash table for dictionary lookups (language to dictionary mapping)
    khash_t(strmap)* hashmap = kh_init(strmap);
    load_dictionary(lang_path, dict_path, hashmap, 'd'); // Load dictionary for decompression

    // Tokenize the compressed input data
    Arena arena = create_arena(data_len / 4); // Initial arena size
    size_t token_count;
    TokenSpan* tokens = tokenize(data_input, data_len, &token_count, &arena);

    // Open output file for writing
    FILE* out = fopen("out.decompressed", "wb");
    if (!out) {
        perror("fopen");
        exit(1);
    }

    // Allocate memory for thread-specific data and output buffers
    ThreadData* thread_data = malloc(sizeof(ThreadData) * threads);
    char** outputs = malloc(sizeof(char*) * threads);
    size_t tokens_per_thread = (token_count + threads - 1) / threads;

    // Parallel region using OpenMP
    #pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num();
        size_t start = tid * tokens_per_thread;
        size_t end = (tid + 1) * tokens_per_thread;
        if (end > token_count) end = token_count;

        // Allocate output buffer for this thread (worst case: original size * 2)
        outputs[tid] = malloc(data_len * 2);
        if (!outputs[tid]) {
            fprintf(stderr, "Thread %d: Failed to allocate output buffer.\n", tid);
            exit(1);
        }

        // Populate ThreadData structure for this thread
        ThreadData data = {
            .input = data_input,
            .tokens = tokens,
            .token_start = start,
            .token_end = end,
            .hashmap = hashmap,
            .escape_char = escape_char,
            .output = outputs[tid]
        };

        process_thread(&data, false); // Execute decompression logic for this thread
        thread_data[tid] = data; // Store the data (including output_len)
    }

    // Write output sequentially from each thread's buffer
    for (int i = 0; i < threads; i++) {
        fwrite(thread_data[i].output, 1, thread_data[i].output_len, out);
        free(outputs[i]); // Free thread's output buffer
    }

    // Cleanup
    fclose(out);
    free(outputs);
    free(thread_data);

    // Free hash table values and destroy the hash map
    khiter_t k;
    for (k = kh_begin(hashmap); k != kh_end(hashmap); ++k) {
        if (kh_exist(hashmap, k)) {
            free((char*)kh_key(hashmap, k)); // Free the key string
            free(kh_value(hashmap, k));      // Free the value string
        }
    }
    kh_destroy(strmap, hashmap);
    destroy_arena(&arena); // Destroy the arena allocator
}

// Main function to parse command-line arguments and call appropriate functions
int main(int argc, char* argv[]) {
    // Check for correct number of arguments
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <-c|-d> <file> <dict> <lang> <threads>\n", argv[0]);
        return 1;
    }

    const char* mode = argv[1];      // Compression or decompression mode
    const char* file_path = argv[2]; // Input file path
    const char* dict_path = argv[3]; // Dictionary file path
    const char* lang_path = argv[4]; // Language file path
    int threads = atoi(argv[5]);     // Number of threads to use

    // Ensure at least one thread is used
    if (threads <= 0) {
        fprintf(stderr, "Number of threads must be positive.\n");
        return 1;
    }

    // Map the input file into memory
    MappedFile input = map_file(file_path);
    if (input.data == NULL && input.size > 0) { // Check for mapping errors
        fprintf(stderr, "Failed to map input file: %s\n", file_path);
        return 1;
    }

    // Call compress or decompress based on mode
    if (strcmp(mode, "-c") == 0) {
        compress(dict_path, lang_path, input.data, input.size, threads);
    } else if (strcmp(mode, "-d") == 0) {
        decompress(dict_path, lang_path, input.data, input.size, threads);
    } else {
        fprintf(stderr, "Invalid mode: use -c (compress) or -d (decompress)\n");
        unmap_file(input);
        return 1;
    }

    unmap_file(input); // Unmap the input file
    return 0;
}

