#!/bin/bash

echo "Checking .c and .h file format ..."
for file in `git diff-index --cached --name-only HEAD | grep -iE '\.(c|h)$' ` ; do
      echo "${file}"
      out=$(git clang-format -v --diff "${file}")
      if [ "$out" != *"no modified files to format"* ] && [ "$out" != *"clang-format did not modify any files"* ]; then
        echo "change to '${file}' that is being committed is formatted incorrectly"
        echo "run 'clang-format -i ${file}' to fix and re-add."
        exit 1;
      fi
done