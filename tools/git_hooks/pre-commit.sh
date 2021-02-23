#!/bin/bash
#################################################################
#                                                               #
# Copyright (c) 2020-2021 Peter Goss All rights reserved.       #
#                                                               #
# Copyright (c) 2020-2021 YottaDB LLC and/or its subsidiaries.  #
# All rights reserved.                                          #
#                                                               #
#   This source code contains the intellectual property         #
#   of its copyright holder(s), and is made available           #
#   under a license.  If you do not know the terms of           #
#   the license, please stop and do not read further.           #
#                                                               #
#################################################################

cd "$(git rev-parse --show-toplevel)" || exit 1
committed_files=$(git diff --cached --name-only HEAD)
changed_files=$(git diff --name-only | tr "\n" " ")
restore_files=""
curyear="$(date +%Y)"
exit_value=0

git stash -q --keep-index

if ! [ -e copyright.py ] ; then
  if ! wget https://gitlab.com/YottaDB/DB/YDB/-/raw/master/ci/copyright.py ; then
    echo "failed to download required script."
    exit_value=1
  fi
fi

echo "Checking that files to be committed have a YottaDB copyright ..."
if [[ $exit_value -eq 0 ]] ; then
  for file in $committed_files ; do
    if [ -e $file ] && bash ./tools/ci/needs_copyright.sh $file && ! grep -q 'Copyright (c) .* YottaDB LLC' $file; then
      echo "    $file requires a YottaDB copyright notice but does not have one."
      exit_value=1
    fi
  done
fi

if [[ exit_value -eq 0 ]] ; then
  echo "Automatically modifying files (copyright updates and code formatting) ..."
  for file in $committed_files ; do
    file_modified=false
    # Deleted files don't need a copyright notice, hence -e check
    if [ -e $file ] && bash ./tools/ci/needs_copyright.sh $file && ! grep -q 'Copyright (c) .*'$curyear' YottaDB LLC' $file; then
        echo "    Updating copyright date in $file"
        cat "$file" | python3 copyright.py > "temp-$file"
        mv "temp-$file" "$file"
        file_modified=true
    fi

    if [[ "$file" == *".c" ]] || [[ "$file" == *".h" ]]; then
      if ! clang-format --dry-run --Werror -style=file "$file" &>/dev/null; then
        echo "    Automatically fixing formatting errors in $file with \"clang-format\"."
        clang-format -i -style=file "$file"
        file_modified=true
      fi
    elif [[ "$file" == *".py" ]] || [[ "$file" == *".pyi" ]]; then
      if ! black -q --check "$file"; then
        echo "    Automatically fixing formatting errors in $file with \"black\"."
        black -q "$file"
        file_modified=true
      fi
    fi

    if $file_modified; then
      git add ${file}
      if ! [[ $changed_files =~ ( |^)$file( |$) ]]; then
        restore_files="$restore_files $file"
      fi
    fi
  done
fi

git stash pop -q
if [[ "${restore_files}" != "" ]]; then
  git restore ${restore_files}
fi
exit $exit_value
