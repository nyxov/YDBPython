#!/bin/bash

echo "Checking .c and .h file format ..."
for file in `git diff-index --cached --name-only HEAD | grep -iE '\.(c|h)$' ` ; do
      echo "${file}"
      out=$(git clang-format -v --diff "${file}")
      if [ "$out" != *"no modified files to format"* ] && [ "$out" != *"clang-format did not modify any files"* ]; then
        if [ "$(git diff ${file})" == "" ]; then
          echo "Automatically fixing formatting errors in ${file} with \"clang-format\"."
          clang-format -i ${file}
          git add ${file}
        else
          echo "File to be committed \"${file}\" has formatting errors that can be fixed automatically;"
          echo "however, there have been changes since it was added, so we will abort commit to avoid conflicts."
        exit 1;
        fi
      fi
done