# FlowerGunKing
FlowerGunKing is a lossless text compressor with the vision of being the best open-source compressor on the internet

# Compile
```
    gcc -o run flowergunking.c -lzstd
```

# Usage
```
    ./run [-c|-d] <input_file> <output_file> [-dict dictionary_file]
```

### Compress
```
    ./run -c dickens compressed -dict dict
```
### De-Compress
```
    ./run -d compressed dickens.restore -dict dict
```
