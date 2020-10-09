#!/bin/bash

MAX_LINE_LENGTH=132

git stash -q --keep-index
committed_files=`git diff --cached --name-only HEAD`

echo "Checking .c and .h file format ..."
commited_c_files=`echo ${committed_files} | tr ' ' '\n ' | grep -iE '\.(c|h)$' `
for file in $commited_c_files ; do
  echo "${file}"
  out=$(git clang-format -v --diff "${file}")
  if [ "$out" != *"no modified files to format"* ] && [ "$out" != *"clang-format did not modify any files"* ]; then
    echo "Automatically fixing formatting errors in ${file} with \"clang-format\"."
    clang-format -i ${file}
    git add ${file}
  fi
done

echo "Checking .py and .pyi file format ..."
commited_py_files=`echo ${committed_files} | tr ' ' '\n ' | grep -iE '\.py(|i)$' `
echo $
for file in $commited_py_files ; do
  echo "${file}"
  black -l $MAX_LINE_LENGTH  -q --check ${file}
  if [ $? == 1 ]; then
    echo "Automatically fixing formatting errors in ${file} with \"black\"."
    black -l $MAX_LINE_LENGTH -q ${file}
    git add ${file}
  fi
done
git stash pop -q