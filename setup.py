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

YDB_DIST = os.environ['ydb_dist']

setup(name = 'yottadb',
      version = '0.0.1',
      ext_modules = [Extension("_yottadb", sources = ['_yottadb.c'],
                               include_dirs=[YDB_DIST], library_dirs=[YDB_DIST],
                               extra_link_args= ["-l", "yottadb", "-l", "ffi"])],
      py_modules = ['yottadb'],
      packages=find_packages(include=['yottadb', 'yottadb.*']),
      package_data={'': ['_yottadb.pyi']},
      include_package_data=True,
      setup_requires=['pytest-runner'],
      tests_require=['pytest'],
      test_suite='test',
     )