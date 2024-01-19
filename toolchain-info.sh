#!/bin/bash

tools=("gcc-9" "g++-9" "otool" "nm")

echo "Platform: `uname -a`"
echo "-------------------------------------"
echo "Tool info:"
echo
for tool in "${tools[@]}"
do
    echo "$tool location: `which $tool`"
    echo "$tool version: `$tool --version`"
    echo
    echo "-------"
    echo
done
