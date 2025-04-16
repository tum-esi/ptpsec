#!/bin/sh
LOG_LEVEL=6
INTERFACE_FLAGS=$(ifconfig | awk -F ':' '/-eth/ {print "-i " $1}' | paste -s -d ' ')
./ptp4l -f ptpsec_slave.cfg -m -l 6 $INTERFACE_FLAGS
# gdb --args ./ptp4l -f ptpsec_slave.cfg -m -l 6 $INTERFACE_FLAGS
