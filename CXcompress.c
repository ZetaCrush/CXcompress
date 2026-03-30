/*
 * CXcompress v1.2.0
 *
 * Fixes applied vs v1.1.0:
 *  1. compress() fast path implemented - O(1) 3D lookup for short words
 *  2. compress_lookup table is now populated in load_dictionary()
 *  3. Decompress hot loop: stack-allocated temp buffer replaces per-token malloc/free
 *  4. Roundtrip test harness added (test mode: -t <input> <dict> <lang> <threads>)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <omp.h>
#include "uthash.h"

#define MAX_LINE    1024
#define MAX_ENTRIES 100000

/* ── symbol presence: is this 1-3 char sequence a known symbol? ── */
bool symbol_lookup[256][256][256] = {{{ false }}};

/* ── decompression: symbol → word (O(1) for short symbols) ── */
char*         word_lookup[256][256][256]     = {{{ NULL }}};
unsigned char word_lookup_len[256][256][256] = {{{ 0 }}};

/* ── FIX #1/#2: compression: word → symbol (O(1) for short words) ── */
char*         compress_lookup[256][256][256]     = {{{ NULL }}};
unsigned char compress_lookup_len[256][256][256] = {{{ 0 }}};

typedef struct {
    char* word;
    char* symbol;
} DictEntry;

typedef struct {
    char*          key;
    char*          value;
    size_t         value_len;
    UT_hash_handle hh;
} HashEntry;

typedef struct {
    size_t start;
    size_t len;
    bool   is_space;
} TokenSpan;

/* ── delimiter predicate ── */
static inline bool is_delimiter(char c) {
    return (c == ' ' || c == 0 || c == ',' || c == '.' ||
            c == '?' || c == '!' || c == '\n' || c == '\r');
}

/* ── dictionary loader ──────────────────────────────────────────────────────
 * mode 'c': builds word→symbol hashmap + compress_lookup for short words
 * mode 'd': builds symbol→word hashmap + word_lookup for short symbols
 * ── */
DictEntry* load_dictionary(const char* dict_path, const char* lang_path,
                           size_t* count, HashEntry** hashmap, const char mode)
{
    FILE* dict_file = fopen(dict_path, "r");
    FILE* lang_file = fopen(lang_path, "r");
    if (!dict_file || !lang_file) {
        fprintf(stderr, "Failed to open dictionary (%s) or language file (%s)\n",
                dict_path, lang_path);
        exit(1);
    }

    DictEntry* entries = malloc(sizeof(DictEntry) * MAX_ENTRIES);
    if (!entries) { fprintf(stderr, "OOM: dictionary\n"); exit(1); }

    char   dict_line[MAX_LINE];
    char   lang_line[MAX_LINE];
    size_t i = 0;

    while (fgets(dict_line, MAX_LINE, dict_file) &&
           fgets(lang_line, MAX_LINE, lang_file))
    {
        dict_line[strcspn(dict_line, "\n")] = 0;
        lang_line[strcspn(lang_line, "\n")] = 0;
        if (!dict_line[0] || !lang_line[0]) continue;

        entries[i].word   = strdup(dict_line);
        entries[i].symbol = strdup(lang_line);

        HashEntry* item = malloc(sizeof(HashEntry));

        if (mode == 'c') {
            /* compression: key=word, value=symbol */
            item->key       = strdup(entries[i].word);
            item->value     = strdup(entries[i].symbol);
            item->value_len = strlen(item->value);

            /* populate symbol_lookup so we can detect collisions at compress time */
            size_t slen = strlen(entries[i].symbol);
            if (slen <= 3) {
                unsigned char a = entries[i].symbol[0];
                unsigned char b = (slen > 1) ? entries[i].symbol[1] : 0;
                unsigned char c = (slen > 2) ? entries[i].symbol[2] : 0;
                symbol_lookup[a][b][c] = true;
            }

            /* FIX #2: populate compress_lookup for O(1) word→symbol on short words */
            size_t wlen = strlen(entries[i].word);
            if (wlen <= 3) {
                unsigned char a = entries[i].word[0];
                unsigned char b = (wlen > 1) ? entries[i].word[1] : 0;
                unsigned char c = (wlen > 2) ? entries[i].word[2] : 0;
                compress_lookup[a][b][c]     = item->value;   /* points into item */
                compress_lookup_len[a][b][c] = (unsigned char)item->value_len;
            }

        } else {
            /* decompression: key=symbol, value=word */
            item->key       = strdup(entries[i].symbol);
            item->value     = strdup(entries[i].word);
            item->value_len = strlen(item->value);

            size_t slen = strlen(entries[i].symbol);
            if (slen <= 3) {
                unsigned char a = entries[i].symbol[0];
                unsigned char b = (slen > 1) ? entries[i].symbol[1] : 0;
                unsigned char c = (slen > 2) ? entries[i].symbol[2] : 0;
                word_lookup[a][b][c]     = item->value;
                word_lookup_len[a][b][c] = (unsigned char)item->value_len;
            }
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
    HashEntry *cur, *tmp;
    HASH_ITER(hh, hashmap, cur, tmp) {
        HASH_DEL(hashmap, cur);
        free(cur->key);
        free(cur->value);
        free(cur);
    }
}

static inline bool is_symbol_fast(const char* word, size_t len) {
    if (len == 0 || len > 3) return false;
    unsigned char a = word[0];
    unsigned char b = (len > 1) ? word[1] : 0;
    unsigned char c = (len > 2) ? word[2] : 0;
    return symbol_lookup[a][b][c];
}

char find_unused_char_from_buffer(const char* buffer, size_t len) {
    bool used[256] = {0};
    used[0] = true;
    for (size_t i = 0; i < len; i++) used[(unsigned char)buffer[i]] = true;
    for (int i = 1; i < 256; i++) if (!used[i]) return (char)i;
    fprintf(stderr, "No escape character available\n");
    exit(1);
}

char* read_file(const char* path, const char* label, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Failed to open %s file: %s\n", label, path); exit(1); }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    rewind(f);
    char* buf = malloc(length + 1);
    size_t rd = fread(buf, 1, length, f);
    buf[rd] = '\0';
    fclose(f);
    if (out_len) *out_len = rd;
    return buf;
}

/* ── COMPRESS ───────────────────────────────────────────────────────────────
 * Changes vs original:
 *   - FIX #1: fast path now actually writes the symbol (was an empty stub)
 *   - FIX #2: compress_lookup is populated in load_dictionary (see above)
 * ── */
void compress(const char* dict_path, const char* lang_path,
              const char* input_buffer, size_t input_len,
              int threads, const char* output_path)
{
    size_t     dict_size = 0;
    HashEntry* hashmap   = NULL;
    DictEntry* dict = load_dictionary(dict_path, lang_path, &dict_size, &hashmap, 'c');

    char  escape_char = find_unused_char_from_buffer(input_buffer, input_len);
    FILE* out         = fopen(output_path, "wb");
    if (!out) { fprintf(stderr, "Failed to open output: %s\n", output_path); exit(1); }
    fputc(escape_char, out);

    /* split on delimiter boundaries */
    size_t  bytes_per_thread = (input_len + threads - 1) / threads;
    size_t* split_points     = malloc(sizeof(size_t) * (threads + 1));
    split_points[0]       = 0;
    split_points[threads] = input_len;
    for (int t = 1; t < threads; t++) {
        size_t pos = t * bytes_per_thread;
        while (pos < input_len && !is_delimiter(input_buffer[pos])) pos++;
        split_points[t] = (pos > input_len) ? input_len : pos;
    }

    char**  segments = malloc(sizeof(char*)  * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));

#pragma omp parallel num_threads(threads)
    {
        int    tid       = omp_get_thread_num();
        size_t start_pos = split_points[tid];
        size_t end_pos   = split_points[tid + 1];

        /* worst case: every byte needs an escape prefix → 2× */
        char*  buffer  = malloc((end_pos - start_pos) * 2 + 1024);
        size_t out_pos = 0;
        size_t i       = start_pos;

        while (i < end_pos) {
            /* pass through delimiters unchanged */
            if (is_delimiter(input_buffer[i])) {
                buffer[out_pos++] = input_buffer[i++];
                continue;
            }

            /* collect word */
            size_t word_start = i;
            while (i < end_pos && !is_delimiter(input_buffer[i])) i++;
            size_t      word_len = i - word_start;
            const char* word_ptr = &input_buffer[word_start];

            /* ── FIX #1: O(1) fast path for short words ── */
            if (word_len <= 3) {
                unsigned char a = (unsigned char)word_ptr[0];
                unsigned char b = (word_len > 1) ? (unsigned char)word_ptr[1] : 0;
                unsigned char c = (word_len > 2) ? (unsigned char)word_ptr[2] : 0;
                char*         sym     = compress_lookup[a][b][c];
                unsigned char sym_len = compress_lookup_len[a][b][c];
                if (sym && sym_len > 0) {
                    memcpy(&buffer[out_pos], sym, sym_len);
                    out_pos += sym_len;
                    continue;
                }
            }

            /* hashmap lookup for longer words (stack temp, no malloc) */
            char temp[256];
            size_t copy_len = (word_len < 255) ? word_len : 255;
            memcpy(temp, word_ptr, copy_len);
            temp[copy_len] = '\0';

            HashEntry* found = NULL;
            HASH_FIND_STR(hashmap, temp, found);

            if (found) {
                memcpy(&buffer[out_pos], found->value, found->value_len);
                out_pos += found->value_len;
            } else {
                /* word not in dict: escape it if it looks like a symbol */
                if (is_symbol_fast(temp, word_len)) {
                    buffer[out_pos++] = escape_char;
                }
                memcpy(&buffer[out_pos], word_ptr, word_len);
                out_pos += word_len;
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
    free(split_points);
    free_dictionary(dict, dict_size);
    free_hashmap(hashmap);
}

/* ── DECOMPRESS ─────────────────────────────────────────────────────────────
 * FIX #3: replaced per-token malloc/free in the hot loop with a stack buffer.
 *          Eliminates allocator contention between OMP threads.
 * ── */
void decompress(const char* dict_path, const char* lang_path,
                const char* input_buffer, size_t input_len,
                int threads, const char* output_path)
{
    if (input_len < 1) return;

    size_t     dict_size = 0;
    HashEntry* hashmap   = NULL;
    DictEntry* dict = load_dictionary(lang_path, dict_path, &dict_size, &hashmap, 'd');

    char         escape_char = input_buffer[0];
    const char*  data        = input_buffer + 1;
    size_t       data_len    = input_len - 1;

    FILE* out = fopen(output_path, "wb");
    if (!out) { fprintf(stderr, "Failed to open output: %s\n", output_path); exit(1); }

    size_t  bytes_per_thread = (data_len + threads - 1) / threads;
    size_t* split_points     = malloc(sizeof(size_t) * (threads + 1));
    split_points[0]       = 0;
    split_points[threads] = data_len;
    for (int t = 1; t < threads; t++) {
        size_t pos = t * bytes_per_thread;
        if (pos >= data_len) {
            for (int r = t; r <= threads; r++) split_points[r] = data_len;
            threads = t;
            break;
        }
        while (pos < data_len && !is_delimiter(data[pos])) pos++;
        split_points[t] = pos;
    }

    char**  segments = malloc(sizeof(char*)  * threads);
    size_t* seg_lens = calloc(threads, sizeof(size_t));

#pragma omp parallel num_threads(threads)
    {
        int    tid       = omp_get_thread_num();
        size_t start_pos = split_points[tid];
        size_t end_pos   = split_points[tid + 1];

        /* decompressed tokens can be longer than compressed ones */
        char*  buffer  = malloc((end_pos - start_pos) * 4 + 1024);
        size_t out_pos = 0;
        size_t i       = start_pos;

        while (i < end_pos) {
            if (is_delimiter(data[i])) {
                buffer[out_pos++] = data[i++];
                continue;
            }

            size_t      token_start = i;
            while (i < end_pos && !is_delimiter(data[i])) i++;
            size_t      token_len = i - token_start;
            const char* token_ptr = &data[token_start];

            bool        is_escaped  = (token_ptr[0] == escape_char);
            const char* actual      = is_escaped ? token_ptr + 1 : token_ptr;
            size_t      actual_len  = token_len - (is_escaped ? 1 : 0);

            if (!is_escaped) {
                /* O(1) fast path for short symbols */
                if (actual_len <= 3) {
                    unsigned char a = (unsigned char)actual[0];
                    unsigned char b = (actual_len > 1) ? (unsigned char)actual[1] : 0;
                    unsigned char c = (actual_len > 2) ? (unsigned char)actual[2] : 0;
                    char* replacement = word_lookup[a][b][c];
                    if (replacement) {
                        size_t repl_len = word_lookup_len[a][b][c];
                        memcpy(&buffer[out_pos], replacement, repl_len);
                        out_pos += repl_len;
                        continue;
                    }
                }

                /* FIX #3: stack-allocated temp buffer — no malloc in hot loop */
                char   temp[256];
                size_t copy_len = (actual_len < 255) ? actual_len : 255;
                memcpy(temp, actual, copy_len);
                temp[copy_len] = '\0';

                HashEntry* found = NULL;
                HASH_FIND_STR(hashmap, temp, found);
                if (found) {
                    memcpy(&buffer[out_pos], found->value, found->value_len);
                    out_pos += found->value_len;
                    continue;
                }
            }

            /* literal passthrough (escaped or not found in dict) */
            memcpy(&buffer[out_pos], actual, actual_len);
            out_pos += actual_len;
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
    free(split_points);
    free_dictionary(dict, dict_size);
    free_hashmap(hashmap);
}

/* ── FIX #4: roundtrip test harness ────────────────────────────────────────
 * Usage: ./CXcompress -t <input> <dict> <lang> <threads>
 *
 * Compresses to a temp file, decompresses back, then byte-compares to the
 * original.  Exits 0 on success, 1 on mismatch, 2 on I/O error.
 * ── */
int run_roundtrip_test(const char* input_path, const char* dict_path,
                       const char* lang_path, int threads)
{
    const char* tmp_compressed   = "/tmp/cxcompress_test_c.bin";
    const char* tmp_decompressed = "/tmp/cxcompress_test_d.txt";

    /* read original */
    size_t orig_len = 0;
    char*  orig     = read_file(input_path, "test input", &orig_len);

    printf("[test] Input:       %s (%zu bytes)\n", input_path, orig_len);

    /* compress */
    compress(dict_path, lang_path, orig, orig_len, threads, tmp_compressed);

    size_t comp_len = 0;
    char*  comp     = read_file(tmp_compressed, "compressed", &comp_len);
    printf("[test] Compressed:  %s (%zu bytes, %.1f%%)\n",
           tmp_compressed, comp_len, 100.0 * comp_len / (orig_len ? orig_len : 1));

    /* decompress */
    decompress(dict_path, lang_path, comp, comp_len, threads, tmp_decompressed);

    size_t decomp_len = 0;
    char*  decomp     = read_file(tmp_decompressed, "decompressed", &decomp_len);
    printf("[test] Decompressed: %s (%zu bytes)\n", tmp_decompressed, decomp_len);

    /* compare */
    int result = 0;
    if (orig_len != decomp_len) {
        fprintf(stderr, "[FAIL] Length mismatch: orig=%zu decomp=%zu\n",
                orig_len, decomp_len);
        result = 1;
    } else if (memcmp(orig, decomp, orig_len) != 0) {
        /* find first byte of divergence */
        size_t diff = 0;
        while (diff < orig_len && orig[diff] == decomp[diff]) diff++;
        fprintf(stderr, "[FAIL] Content mismatch at byte %zu  "
                        "orig=0x%02x decomp=0x%02x\n",
                diff, (unsigned char)orig[diff], (unsigned char)decomp[diff]);
        result = 1;
    } else {
        printf("[PASS] Roundtrip OK — %zu bytes match exactly.\n", orig_len);
    }

    free(orig);
    free(comp);
    free(decomp);
    return result;
}

/* ── MAIN ── */
int main(int argc, char* argv[]) {
    if (argc < 2) goto usage;

    if (strcmp(argv[1], "-t") == 0) {
        /* test mode: -t <input> <dict> <lang> <threads> */
        if (argc != 6) goto usage;
        int threads = atoi(argv[5]);
        return run_roundtrip_test(argv[2], argv[3], argv[4], threads);
    }

    if (argc != 7) goto usage;

    const char* mode_flag    = argv[1];
    const char* file_path    = argv[2];
    const char* dict_path    = argv[3];
    const char* language_path = argv[4];
    int         threads      = atoi(argv[5]);
    const char* output_path  = argv[6];

    size_t input_len    = 0;
    char*  input_buffer = read_file(file_path, "input", &input_len);

    if (strcmp(mode_flag, "-c") == 0) {
        compress(dict_path, language_path, input_buffer, input_len, threads, output_path);
    } else if (strcmp(mode_flag, "-d") == 0) {
        decompress(language_path, dict_path, input_buffer, input_len, threads, output_path);
    } else {
        free(input_buffer);
        goto usage;
    }

    free(input_buffer);
    return 0;

usage:
    fprintf(stderr,
        "Usage:\n"
        "  %s -c <input> <dict> <lang> <threads> <output>   compress\n"
        "  %s -d <input> <dict> <lang> <threads> <output>   decompress\n"
        "  %s -t <input> <dict> <lang> <threads>             roundtrip test\n",
        argv[0], argv[0], argv[0]);
    return 1;
}
