#################################################################
#                                                               #
# Copyright (c) 2019-2020 Peter Goss All rights reserved.       #
#                                                               #
# Copyright (c) 2019-2020 YottaDB LLC and/or its subsidiaries.  #
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
import csv
from typing import Dict

YDB_DIST = os.environ["ydb_dist"]
ERROR_DEF_FILES = ["libydberrors.h", "libydberrors2.h"]
ERROR_NAME_ALTERATIONS_FILE = "error_name_alterations.csv"


def get_alteration_dict(filename: str) -> Dict[str, str]:
    alterations = {}
    alterations_file = pathlib.Path(".") / filename
    with alterations_file.open() as f:
        reader = csv.reader(f)
        for row in reader:
            alterations[row[0]] = row[1]
    return alterations


def create_exceptions_from_error_codes():
    YDB_Dir = pathlib.Path(YDB_DIST)
    exceptions_header = pathlib.Path(".") / "_yottadbexceptions.h"
    exception_data = []
    alterations = get_alteration_dict(ERROR_NAME_ALTERATIONS_FILE)
    for filename in ERROR_DEF_FILES:
        file_path = YDB_Dir / filename
        with file_path.open() as file:
            for line in file.readlines():
                if line[0:7] == "#define":
                    exception_info = {}
                    parts = line.split()
                    exception_info["c_name"] = parts[1]
                    error_id = exception_info["c_name"].split("_")[2]
                    if error_id in alterations.keys():
                        error_id = alterations[error_id]
                    exception_info["python_name"] = f"YDB{error_id}Error"
                    exception_data.append(exception_info)

    header_file_text = ""
    # define exceptions
    for exception_info in exception_data:
        header_file_text += f'static PyObject *{exception_info["python_name"]};\n'
    header_file_text += "\n"
    # create macro to add exceptions to module
    header_file_text += "#define ADD_YDBERRORS() { \\\n"
    add_exception_template = (
        '    {python_name} = PyErr_NewException("_yottadb.{python_name}", YDBError, NULL); \\\n'
        + '    PyModule_AddObject(module, "{python_name}", {python_name}); \\\n'
    )
    for exception_info in exception_data:
        header_file_text += add_exception_template.replace("{python_name}", exception_info["python_name"])
    header_file_text += "}\n"
    header_file_text += "\n"
    # create macro to test for and raise exception
    header_file_text += "#define RAISE_SPECIFIC_ERROR(STATUS, MESSAGE) { \\\n"
    header_file_text += "    assert(YDB_OK != STATUS); \\\n"
    header_file_text += "    assert(NULL != MESSAGE); \\\n"
    header_file_text += "    "
    test_status_template = "if ({c_name} == STATUS) \\\n" + "        PyErr_SetObject({python_name}, MESSAGE); \\\n" + "    else "

    for exception_info in exception_data:
        test_status = test_status_template.replace("{python_name}", exception_info["python_name"])
        test_status = test_status.replace("{c_name}", exception_info["c_name"])
        header_file_text += test_status
    header_file_text += "\\\n        PyErr_SetObject(YDBError, MESSAGE); \\\n"
    header_file_text += "}\n"

    with exceptions_header.open("w") as header_file:
        header_file.write(header_file_text)


create_exceptions_from_error_codes()

setup(
    name="yottadb",
    version="0.0.1",
    ext_modules=[
        Extension(
            "_yottadb",
            sources=["_yottadb.c"],
            include_dirs=[YDB_DIST],
            library_dirs=[YDB_DIST],
            extra_link_args=["-l", "yottadb", "-l", "ffi"],
            extra_compile_args=["--std=c99"],
        )
    ],
    py_modules=["_yottadb", "yottadb"],
    packages=find_packages(include=["_yottadb", "_yottadb.*", "yottadb"]),
    package_data={"": ["_yottadb.pyi"]},
    include_package_data=True,
    setup_requires=["pytest-runner"],
    tests_require=["pytest"],
    test_suite="test",
)
