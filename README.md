# FlowerGunKing
FlowerGunKing is a lossless text compressor with the vision of being the best open-source compressor on the internet

# Compile
```
    gcc -o flowergunking flowergunking.c -lzstd
```

# Usage
```
    ./flowergunking [-c|-d] <input_file> <output_file> [-dict dictionary_file]
```

### Compress
```
    ./flowergunking -c dickens compressed -dict dict
```
### De-Compress
```
    ./flowergunking -d compressed dickens.restore -dict dict
```
