#!/bin/sh
LOG_LEVEL=6
INTERFACE_FLAGS=$(ifconfig | awk -F ':' '/-eth/ {print "-i " $1}' | paste -s -d ' ')
./ptp4l -f ptpsec_master.cfg -m -l $LOG_LEVEL $INTERFACE_FLAGS
