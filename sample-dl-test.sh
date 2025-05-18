#!/bin/sh

FURL=https://github.com/MiloszKrajewski/SilesiaCorpus/raw/master/dickens.zip
EXT=.zip
arch=`basename "$FURL"`
file=${arch%%$EXT}
echo $file

if [ ! -f $file ]; then
    wget "$FURL" && \
        unzip $arch && \
        rm $arch
fi

./cxc-cd-test.sh $file

