# FlowerGunKing
FlowerGunKing is a lossless text compressor with the vision of being the best open-source compressor on the internet

This software is currently available as a standalone python script; work is being done to port to C++. This compression library is built on top of the very popular zstd created by Yann Collet.

# Usage
### Compression
```
python flowergunking.py -c --f <file_to_compress> --e <file_encoding> --l <zstd_compression_level> --dict <dictionary_file>
```

### Decompression
```
python flowergunking.py -d --f <file_to_decompress> --e <file_encoding_to_write_restored> --dict <dictionary_file>
```
