# CXcompress v1.0.1
CXcompress is a lossless text compressor with the vision of being the best open-source preprocessing compressor on the internet

This compression library is built to be used before the very popular zstd created by Yann Collet at Meta. CXcompress can also be used as a preprocessing step for other compressors like cmix, zlib, or lzma for improved performance

## Tested on 10.2MB Charles Dickens dataset (a.k.a. dickens)
| Compression Method             | Compressed Size | Time Taken (s) |
|-------------------------------|----------------:|---------------:|
| Zstd (level 19 with dictionary)               |        3,212,810 |          2.723 |
| CXcompress + Zstd (level 19 with dictionary)  |        2,836,003 |          1.631 |

# Algorithm
This is a dictionary compression algorithm; words are replaced with combinations of letters. Differing from other algorithms, the order of the letter symbols in the dictionary are determined by their frequency in text. A pre-determined order is used to save processing time.

This dictionary structure increases the Zipfian characteristics of the transformed data, making it easier to compress

# Usage (supports MacOS/Linux)
### Compilation
```
gcc-14 -Wall -O3 -fopenmp CXcompress.c -o CXcompress
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
The runtime of the compressor will be slower only the first time you run it; after that it will be fast for all files due to caching/initilization

The compressor will exit without compressing under rare scenarios. To use this in production, compress with other algorithm alone if this happens

While using zstd after CXcompress, train a zstd dictionary on 100 copies of the language pack file you are using and call zstd with this dictionary

## TODO
1. 📚 More prebuilt dictionaries and language packs
2. 🚀 Add CUDA support for massively parallel operation with GPUs
3. 🔨 Algorithmic development

## License TL;DR
This software is free for non-commercial use. For commercial use, we are offering a free 6 month trial to early customers then a licensing fee of 12% of cost savings
