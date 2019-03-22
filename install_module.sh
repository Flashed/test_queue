#!/bin/bash
set +x

insmod test_queue.ko

sleep 1

PUSH_MAJ="$(cat /proc/devices | grep queue_push | grep -Eo '[0-9]{1,4}')"
POP_MAJ="$(cat /proc/devices | grep queue_pop | grep -Eo '[0-9]{1,4}')"

mknod /dev/queue_push c  $PUSH_MAJ 0
chmod 666 /dev/queue_push
mknod /dev/queue_pop c  $POP_MAJ 0
chmod 666 /dev/queue_pop


