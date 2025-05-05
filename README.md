# FlowerGunKing
FlowerGunKing is a lossless text compressor with the vision of being the best open-source compressor on the internet

This software is currently available as an exe created from python via pyinstaller; work is being done to port to C++. This compression library is built on top of the very popular zstd created by Yann Collet.

# Compilation
```
pyinstaller --onefile --name flowergunking flowergunking.py
```
The executable will be located in the dist/ folder

# Usage
### Compression
```
./flowergunking -c --f <file_to_compress> --e <file_encoding> --l <zstd_compression_level> --dict <dictionary_file>
```

### Decompression
```
./flowergunking -d --f <file_to_decompress> --e <file_encoding_to_write_restored> --dict <dictionary_file>
```
