#!/bin/sh

if [ $# -eq 0 ]
  then
    echo "Compress and decompress given (text) file"
    echo "Usage: $0 <file>"
    exit 0
fi

file="$1"
echo testing $file...

./CXcompress -c $file dict lang $(nproc)
./CXcompress -d $file.cxc dict lang $(nproc)

cmp $file $file.cxc.dec
if [ $? -eq 0 ]; then
    echo OK
else
    echo NOK
fi
