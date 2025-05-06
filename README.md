# CXcompress
CXcompress is a lossless text compressor with the vision of being the best preprocessing compressor on the internet

This compression library is built to be used on top of the very popular zstd created by Yann Collet at Meta. CXcompress can also be used as a preprocessing step for other compressors like zlib and lzma

# Usage (supports MacOS/Linux)
### Compilation
```
gcc-14 -O3 -march=native -flto -fopenmp -o CXcompress CXcompress.c
```

### Compression
```
./CXcompress -c <input_file> <dictionary_file> <language_pack_int> <num_threads>
```
You can use the "dict" file as a prebuilt English dictionary and the prebuilt "0" English language pack

### Decompression
```
./CXcompress -d <compressed_file> <dictionary_file> <language_pack_int> <num_threads>
```

## Notes
To check how many threads are available on your machine, run this command:
```
sysctl -n hw.logicalcpu
```
The runtime of the compressor will be slower only the first time you run it; after that it will be fast due to caching/initilization

## TODO
1. Add OpenCL support for massively parallel operation with GPUs
2. More prebuilt dictionaries and language packs
3. Algorithmic development
