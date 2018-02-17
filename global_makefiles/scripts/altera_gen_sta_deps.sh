#!/bin/bash

# Check arguments.
if [ "$1" == "" ]; then
    echo "USAGE: $0 project"
    exit 1
fi
if [ ! -f $1.qsf ]; then
    echo "ERROR: file does not exist"
    exit 1
fi

# Get source files from Quartus settings file.
SDC_FILES=`grep "set_global_assignment -name SDC_FILE" $1.qsf | awk '{print $4}' | tr -d \" | tr "\n" " "`
TCL_FILES=`grep "set_global_assignment -name TIMEQUEST_REPORT_SCRIPT" $1.qsf | awk '{print $4}' | tr -d \" | tr "\n" " "`

# Generate dependencies:
for file in $SDC_FILES $TCL_FILES ; do
    echo "$1.sta.rpt : $file"
done

# Done.
exit 0
