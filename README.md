# cxcompress
Cxcompress is a lossless text compressor with the vision of being the best open-source compressor on the internet

# Compile
```
    gcc -o run cxcompress.c -lzstd
```

# Usage
```
    ./run [-c|-d] <input_file> <output_file> [-dict dictionary_file]
```

### Compress
```
    .\run -c dickens compressed -dict dict
```
### De-Compress
```
    .\run -d compressed dickens.restore -dict dict
```

# Check Result
Use compare.exe for checking the accuracy of compression
```
    .\compare.exe
    dickens
    dickens.restore
```
Check compare_result file for checking the difference.
<br />
``If It's empty, this means original file and restored file is 100% equal.``
