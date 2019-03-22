#!/bin/bash

#1024+ files 
for i in {1..2000}
do
    dd of=mess$i.data if=/dev/urandom bs=60k count=1
    cat mess$i.data > /dev/queue_push
    rm mess$i.data
done

#One small file
dd of=mess0.data if=/dev/urandom bs=6 count=1
cat mess0.data > /dev/queue_push
rm mess0.data

#Too big file (must be error No disk space!)
dd of=mess0.data if=/dev/urandom bs=65k count=1
cat mess0.data > /dev/queue_push
rm mess0.data

