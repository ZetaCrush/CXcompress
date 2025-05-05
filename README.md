# CXcompress
CXcompress is a lossless text compressor with the vision of being the best open-source compressor on the internet

This compression library is built to be used on top of the very popular zstd created by Yann Collet at Meta. CXcompress can also be used as a preprocessing step for other compressors like zlib and lzma.

# Usage (supports MacOS/Linux)
### Compilation
```
gcc-14 -O3 -march=native -flto -fopenmp -o CXcompress CXcompress.c
```

### Compression
```
./CXcompress -c <input_file> <dictionary_file> <language_pack_int> <num_threads>
```

### Decompression
```
./CXcompress -d <compressed_file> <dictionary_file> <language_pack_int> <num_threads>
```
