#################################################################
#                                                               #
# Copyright (c) 2019 Peter Goss All rights reserved.            #
#                                                               #
# Copyright (c) 2019 YottaDB LLC and/or its subsidiaries.       #
# All rights reserved.                                          #
#                                                               #
#   This source code contains the intellectual property         #
#   of its copyright holder(s), and is made available           #
#   under a license.  If you do not know the terms of           #
#   the license, please stop and do not read further.           #
#                                                               #
#################################################################
from setuptools import setup, Extension, find_packages
import os
import pathlib

YDB_DIST = os.environ['ydb_dist']
ERROR_DEF_FILES = ['libydberrors.h',  'libydberrors2.h']


def create_error_code_module():
    YDB_Dir = pathlib.Path(YDB_DIST)
    py_error_file = pathlib.Path('.') / "_yottadb" / "errors.py"
    with py_error_file.open('w') as py_file:
        for filename in ERROR_DEF_FILES:
            file_path = YDB_Dir / filename
            with file_path.open() as file:
                for line in file.readlines():
                    if line[0:7] == "#define":
                        parts = line.split()
                        py_file.write(f'{parts[1]} = {parts[2]}\n')


create_error_code_module()

setup(name = 'yottadb',
      version = '0.0.1',
      ext_modules = [Extension("_yottadb_wrapper", sources = ['_yottadb.c'],
                               include_dirs=[YDB_DIST], library_dirs=[YDB_DIST],
                               extra_link_args= ["-l", "yottadb", "-l", "ffi"])],
      py_modules = ['_yottadb', 'yottadb'],
      packages=find_packages(include=['_yottadb', '_yottadb.*', 'yottadb', 'yottadb.*']),
      package_data={'': ['_yottadb.pyi']},
      include_package_data=True,
      setup_requires=['pytest-runner'],
      tests_require=['pytest'],
      test_suite='test',
     )