/****************************************************************
 *                                                              *
 * Copyright (c) 2019 Peter Goss All rights reserved.           *
 *                                                              *
 * Copyright (c) 2019 YottaDB LLC and/or its subsidiaries.      *
 * All rights reserved.                                         *
 *                                                              *
 *  This source code contains the intellectual property         *
 *  of its copyright holder(s), and is made available           *
 *  under a license.  If you do not know the terms of           *
 *  the license, please stop and do not read further.           *
 *                                                              *
 ****************************************************************/

#include <stdbool.h>
#include <assert.h>
#include <Python.h>
#include <libyottadb.h>
#include <ffi.h>

#include "_yottadb.h"
#include "_yottadbexceptions.h"


/* LOCAL UTILITY FUNCTIONS */

/* ARRAY OF YDB_BUFFER_T UTILITIES */

/* Routine to create an array of empty ydb_buffer_ts with num elements each with an allocated length of len
 *
 * Parameters:
 *   num    - the number of buffers to allocate in the array
 *   len    - the length of the string to allocate for each of the the ydb_buffer_ts
 *
 * free with FREE_BUFFER_ARRAY macro
 */
static ydb_buffer_t* empty_buffer_array(int num, int len) {
    int i;
    ydb_buffer_t *return_buffer_array;

    return_buffer_array = (ydb_buffer_t*)calloc(num, sizeof(ydb_buffer_t));
    for(i = 0; i < num; i++)
        YDB_MALLOC_BUFFER(&return_buffer_array[i], len);

    return return_buffer_array;
}

/* Routine to free an array of ydb_buffer_ts
 *
 * Parameters:
 *   array     - pointer to the array of ydb_buffer_ts to be freed.
 *     len    - number of elements in the array to be freed
 *
 */
//static void FREE_BUFFER_ARRAY(ydb_buffer_t *array, int len) {
//    for(int i = 0; i < len; i++)
//        YDB_FREE_BUFFER(&((ydb_buffer_t*)array)[i]);
//}


/* UTILITIES TO CONVERT BETWEEN SEQUENCES OF PYUNICODE STRING OBJECTS AND AN ARRAY OF YDB_BUFFER_TS */

/* Routine to validate that the PyObject passed to it is indeed an array of python bytes objects.
 *
 * Parameters:
 *   sequence    - the python object to check.
 */
static bool validate_sequence_of_bytes(PyObject *sequence) {
    bool ret = true;
    int i, len_seq;
    PyObject *item, *seq;

    seq = PySequence_Fast(sequence, "argument must be iterable");
    if (!seq || PyBytes_Check(sequence)) /* PyBytes it's self is a sequence */
        return false;

    len_seq = PySequence_Fast_GET_SIZE(seq);
    for (i=0; i < len_seq; i++) { /* check each item for a bytes object */
        item = PySequence_Fast_GET_ITEM(seq, i);
        if (!PyBytes_Check(item))
            ret = false;
        if (!ret)
            break;
    }
    Py_DECREF(seq);
    return ret;
}

/* Routine to validate a 'subsarray' argument in many of the wrapped functions. The 'subsarray' argument must be a sequence
 * of Python bytes objects or a Python None. Will set Exception String and return 'false' if invalid and return 'true' otherwise.
 * (Calling function is expected to return NULL to cause the Exception to be raised.)
 *
 * Parameters:
 *   subsarray    - the Python object to validate.
 */
static bool validate_subsarray_object(PyObject *subsarray) {
     if (Py_None != subsarray) {
        if (!validate_sequence_of_bytes(subsarray)) {
            /* raise TypeError */
            PyErr_SetString(PyExc_TypeError, "'subsarray' must be a Sequence (e.g. List or Tuple) containing only bytes or None");
            return false;
        }
    }
    return true;
}

/* Routine to convert a sequence of Python bytes into a C array of ydb_buffer_ts. Routine assumes sequence was already
 * validated with 'validate_sequence_of_bytes' function. The function creates a copy of each Python bytes' data so
 * the resulting array should be freed by using the 'FREE_BUFFER_ARRAY' function.
 *
 * Parameters:
 *    sequence    - a Python Object that is expected to be a Python Sequence containing Strings.
 */
bool convert_py_bytes_sequence_to_ydb_buffer_array(PyObject *sequence, int sequence_len, ydb_buffer_t *buffer_array) {
    bool done;
    int bytes_len;
    char *bytes_c;
    PyObject *bytes, *seq;

    seq = PySequence_Fast(sequence, "argument must be iterable");
    if (!seq) {
        PyErr_SetString(YDBPythonError, "Can't cunvert none sequence to buffer array.");
        return false;
    }

    for(int i = 0; i < sequence_len; i++) {
        bytes = PySequence_Fast_GET_ITEM(seq, i);
        bytes_len = PyBytes_Size(bytes);
        bytes_c = PyBytes_AsString(bytes);
        YDB_MALLOC_BUFFER(&buffer_array[i], bytes_len);
        YDB_COPY_BYTES_TO_BUFFER(bytes_c, bytes_len, &buffer_array[i], done);
        if (false == done) {
            for (int j = 0; j < i; j ++)
                YDB_FREE_BUFFER(&buffer_array[j])
            Py_DECREF(seq);
            PyErr_SetString(YDBPythonError, "failed to copy bytes object to buffer array");
            return false;
        }
    }

    Py_DECREF(seq);
    return true;
}

/* converts an array of ydb_buffer_ts into a sequence (Tuple) of Python strings.
 *
 * Parameters:
 *    buffer_array        - a C array of ydb_buffer_ts
 *    len                - the length of the above array
 */
PyObject* convert_ydb_buffer_array_to_py_tuple(ydb_buffer_t *buffer_array, int len) {
    int i;
    PyObject *return_tuple;

    return_tuple = PyTuple_New(len);
    for(i=0; i < len; i++)
        PyTuple_SetItem(return_tuple, i, Py_BuildValue("y#", buffer_array[i].buf_addr, buffer_array[i].len_used));

    return return_tuple;
}

/* UTILITIES TO CONVERT BETWEEN DATABASE KEYS REPRESENTED USING PYTHON OBJECTS AND YDB C API TYPES */

/* The workhorse routine of a couple of routines that convert from Python objects (varname and subsarray) to YDB keys.
 *
 * Parameters:
 *    dest        - pointer to the YDBKey to fill.
 *    varname    - Python Bytes object representing the varname
 *    subsarray    - array of python Bytes objects representing the array of subcripts
 */

static bool load_YDBKey(YDBKey *dest, PyObject *varname, PyObject *subsarray) {
    bool copy_success, convert_success;
    int len;
    char* bytes_c;
    ydb_buffer_t *varname_y, *subsarray_y;

    len = PyBytes_Size(varname);
    bytes_c = PyBytes_AsString(varname);

    varname_y = (ydb_buffer_t*)calloc(1, sizeof(ydb_buffer_t));
    YDB_MALLOC_BUFFER(varname_y, len);
    YDB_COPY_BYTES_TO_BUFFER(bytes_c, len, varname_y, copy_success);
    if (!copy_success) {
        YDB_FREE_BUFFER(varname_y);
        free(varname_y);
        PyErr_SetString(YDBPythonError, "failed to copy bytes object to buffer");
        return false;
    }

    dest->varname = varname_y;
    if (Py_None != subsarray) {
        dest->subs_used = PySequence_Length(subsarray);
        subsarray_y = (ydb_buffer_t*)calloc(dest->subs_used, sizeof(ydb_buffer_t));
        convert_success = convert_py_bytes_sequence_to_ydb_buffer_array(subsarray, dest->subs_used, subsarray_y);
        if (convert_success) {
            dest->subsarray = subsarray_y;
        } else {
            YDB_FREE_BUFFER(varname_y);
            FREE_BUFFER_ARRAY(varname_y, dest->subs_used);
            PyErr_SetString(YDBPythonError, "failed to covert sequence to buffer array");
            return false;
        }
    } else {
        dest->subs_used = 0;
    }
    return true;
}

/* Routine to free a YDBKey structure.
 *
 * Parameters:
 *    key    - pointer to the YDBKey to free.
 */
static void free_YDBKey(YDBKey* key) {
    int i;

    YDB_FREE_BUFFER((key->varname));
    for (i=0; i < key->subs_used; i++)
        YDB_FREE_BUFFER(&((ydb_buffer_t*)key->subsarray)[i]);
}

/* Routine to validate a sequence of Python sequences representing keys. (Used only by lock().)
 * Validation rule:
 *        1) key_sequence must be a sequence
 *        2) each item in key_sequence must be a sequence
 *        3) each item must be a sequence of 1 or 2 sub-items.
 *        4) item[0] must be a bytes object.
 *        5) item[1] either does not exist, is None or a sequence
 *        6) if item[1] is a sequence then it must contain only bytes objects.
 *
 * Parameters:
 *    keys_sequence        - a Python object that is to be validated.
 */
static bool validate_py_keys_sequence_bytes(PyObject* keys_sequence) {
    bool ret = true;
    int i, len_keys, len_key_seq;
    PyObject *key, *varname, *subsarray, *seq, *key_seq;

    seq = PySequence_Fast(keys_sequence, "'keys' argument must be a Sequence");
    if (!seq)
        return false;

    len_keys = PySequence_Fast_GET_SIZE(seq);
    for (i=0; i < len_keys; i++) {
        key = PySequence_Fast_GET_ITEM(seq, i);
        key_seq = PySequence_Fast(key, "");
        if (!key_seq) {
            PyErr_Format(PyExc_TypeError, "item %d in 'keys' sequence is not a sequence", i);
            ret = false;
            break;
        }

        len_key_seq = PySequence_Fast_GET_SIZE(key_seq);
        if ((1 != len_key_seq) && (2 != len_key_seq)) {
            PyErr_Format(PyExc_TypeError, "item %d in 'keys' sequence is not a sequence of 1 or 2 items", i);
            ret = false;
        }

        varname = PySequence_Fast_GET_ITEM(key_seq, 0);
        if (!PyBytes_Check(varname)) {
            PyErr_Format(PyExc_TypeError, "the first value in item %d of 'keys' sequence must be a bytes object", i);
            ret = false;
        }

        if (2 == len_key_seq) {
            subsarray = PySequence_Fast_GET_ITEM(key, 1);
            if (!validate_subsarray_object(subsarray)) {
                /* overwrite Exception string set by 'validate_subsarray_object' to be appropriate for lock context */
                PyErr_Format(PyExc_TypeError,
                                "the second value in item %d of 'keys' sequence must be a sequence of bytes or None", i);
                ret = false;
            }
        }
        Py_DECREF(key_seq);
        if (!ret)
            break;
    }
    Py_DECREF(seq);
    return true;
}

/* Routine to covert a sequence of keys in Python sequences and bytes to an array of YDBKeys. Assumes that the input
 * has already been validated with 'validate_py_keys_sequence' above. Use 'free_YDBKey_array' below to free the returned
 * value.
 *
 * Parameters:
 *    sequence    - a Python object that has already been validated with 'validate_py_keys_sequence' or equivalent.
 */
static bool convert_key_sequence_to_YDBKey_array(PyObject* sequence, YDBKey* ret_keys) {
    bool success = true;
    int i, len_keys;
    PyObject *key, *varname, *subsarray, *seq, *key_seq;

    seq = PySequence_Fast(sequence, "argument must be iterable");
    len_keys = PySequence_Fast_GET_SIZE(seq);

    for (i=0; i < len_keys; i++) {
        key = PySequence_Fast_GET_ITEM(seq, i);
        key_seq = PySequence_Fast(key, "argument must be iterable");
        varname = PySequence_Fast_GET_ITEM(key_seq, 0);
        subsarray = Py_None;

        if (2 == PySequence_Fast_GET_SIZE(key_seq))
            subsarray = PySequence_Fast_GET_ITEM(key_seq, 1);
        success = load_YDBKey(&ret_keys[i], varname, subsarray);
        Py_DECREF(key_seq);
        if (!success)
            break;

    }
    Py_DECREF(seq);
    return success;
}

/* Routine to free an array of YDBKeys as returned by above 'convert_key_sequence_to_YDBKey_array'.
 *
 * Parameters:
 *    keysarray    - the array that is to be freed.
 *    len        - the number of elements in keysarray.
 */
static void free_YDBKey_array(YDBKey* keysarray, int len) {
    int i;
    for(i = 0; i < len; i++)
        free_YDBKey(&keysarray[i]);
    free(keysarray);
}

/* Routine to help raise a YDBError. The caller still needs to return NULL for the Exception to be raised.
 * This routine will check if the message has been set in the error_string_buffer and look it up if not.
 *
 * Parameters:
 *    status                - the error code that is returned by the wrapped ydb_ function.
 *    error_string_buffer    - a ydb_buffer_t that may or may not contain the error message.
 */
static void raise_YDBError(int status, ydb_buffer_t* error_string_buffer, int tp_token) {
    ydb_buffer_t ignored_buffer;
    PyObject *message;
    char full_error_message[YDB_MAX_ERRORMSG];
    char *error_name, *error_message, *err_field1, *err_field2, *err_field3, *err_field4;
    char *next_field = NULL;
    const char *delim = ",";

    if (0 == error_string_buffer->len_used) {
        YDB_MALLOC_BUFFER(&ignored_buffer, YDB_MAX_ERRORMSG);
        ydb_message_t(tp_token, &ignored_buffer, status, error_string_buffer);
    }

    if (0 != error_string_buffer->len_used) {
        error_string_buffer->buf_addr[error_string_buffer->len_used] = '\0';
        err_field1 = strtok_r(error_string_buffer->buf_addr, delim, &next_field);
        err_field2 = strtok_r(NULL, delim, &next_field);
        err_field3 = strtok_r(NULL, delim, &next_field);
        err_field4 = strtok_r(NULL, delim, &next_field);
        if (NULL != err_field4) {
            error_name = err_field3;
            error_message = err_field4;
        } else  {
            error_name = err_field1;
            error_message = err_field2;
        }
        if (NULL == error_name)
            error_name = "UNKNOWN";
        if (NULL == error_message)
            error_message = "";
    } else if (YDB_TP_ROLLBACK == status){
        error_name = "%YDB-TP-ROLLBACK";
        error_message = " Transaction callback function returned YDB_TP_ROLLBACK.";
    } else {
        error_name = "UNKNOWN";
        error_message = "";
    }

    snprintf(full_error_message, YDB_MAX_ERRORMSG, "%s (%d):%s", error_name, status, error_message);
    message = Py_BuildValue("s", full_error_message);

    RAISE_SPECIFIC_ERROR(status, message);
}


/* SIMPLE AND SIMPLE THREADED API WRAPPERS */

/* FOR ALL PROXY FUNCTIONS BELOW
 * do almost nothing themselves, simply calls wrapper with a flag for which API they mean to call.
 *
 * Parameters:
 *
 *    self        - the object that this method belongs to (in this case it's the _yottadb module.
 *    args        - a Python tuple of the positional arguments passed to the function
 *    kwds        - a Python dictonary of the keyword arguments passed the tho function
 */

/* FOR ALL BELOW WRAPPERS:
 * does all the work to wrap the 2 related functions using the threaded flag to make the few modifications related how the
 * simple and simple threaded APIs are different.
 *
 * Parameters:
 *    self, args, kwds    - same as proxy functions.
 *    threaded             - either true for simple_treaded api or false used by the proxy functions to indicate which API was being called.
 *
 * FOR ALL
 */

/* Wrapper for ydb_data_s and ydb_data_st. */
static PyObject* data(PyObject* self, PyObject* args, PyObject* kwds) {
    bool return_NULL = false;
    char *varname;
    int varname_len, subs_used, status;
    unsigned int ret_value;
    uint64_t tp_token;
    PyObject *subsarray, *return_python_int;
    ydb_buffer_t error_string_buffer, varname_y, *subsarray_y;

    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char *kwlist[] = {"varname", "subsarray", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|OK", kwlist, &varname, &varname_len, &subsarray, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "data()", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_data_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_value);

        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }

        /* Create Python object to return */
        if (!return_NULL)
            return_python_int = Py_BuildValue("I", ret_value);
    }

    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer);

    if (return_NULL)
        return NULL;
    else
        return return_python_int;
}

/* Wrapper for ydb_delete_s() and ydb_delete_st() */
static PyObject* delete_wrapper(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int deltype, status, varname_len, subs_used;
    char *varname;
    uint64_t tp_token;
    PyObject *subsarray;
    ydb_buffer_t error_string_buffer, varname_y, *subsarray_y;

    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;
    deltype = YDB_DEL_NODE;

    /* parse and validate */
    static char* kwlist[] = {"varname", "subsarray", "delete_type", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|OiK", kwlist, &varname, &varname_len, &subsarray, &deltype, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    if ((deltype != YDB_DEL_NODE) && (deltype != YDB_DEL_TREE)) {
        // 'deltype' is being set to something other than the default by 'PyArg_ParseTupleAndKeywords' when 'delete_type' argument
        // is not used. This seems like a bug.
        deltype = YDB_DEL_NODE;
    }

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "delete_wrapper()", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_delete_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, deltype);


        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }
    }

    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer)
    if (return_NULL) {
        return NULL;
    } else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}

/* Wrapper for ydb_delete_excl_s() and ydb_delete_excl_st() */
static PyObject* delete_excel(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    bool success;
    int namecount, status;
    uint64_t tp_token;
    PyObject *varnames;
    ydb_buffer_t error_string_buffer;

    /* Defaults for non-required arguments */
    varnames = Py_None;
    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char* kwlist[] = {"varnames", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OK", kwlist, &varnames, &tp_token))
        return NULL;

    /* Setup for Call */
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);
    namecount = 0;
    if (Py_None != varnames)
        namecount = PySequence_Length(varnames);
    ydb_buffer_t varnames_ydb[namecount];
    if (0 < namecount) {
        success = convert_py_bytes_sequence_to_ydb_buffer_array(varnames, namecount, varnames_ydb);
        if (!success)
            return NULL;
    }

    status = ydb_delete_excl_st(tp_token, &error_string_buffer, namecount, varnames_ydb);

    /* check status for Errors and Raise Exception */
    if (YDB_OK != status) {
        raise_YDBError(status, &error_string_buffer, tp_token);
        /* free allocated memory */
        return_NULL = true;
    }

    /* free allocated memory */
    FREE_BUFFER_ARRAY(varnames_ydb, namecount);
    YDB_FREE_BUFFER(&error_string_buffer);
    if (return_NULL) {
        return NULL;
    } else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}

/* Wrapper for ydb_get_s() and ydb_get_st() */
static PyObject* get(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int subs_used, status, return_length, varname_len;
    char *varname;
    uint64_t tp_token;
    PyObject *subsarray, *return_python_string;
    ydb_buffer_t varname_y, error_string_buffer, ret_value, *subsarray_y;

    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char* kwlist[] = {"varname", "subsarray", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|OK", kwlist, &varname, &varname_len, &subsarray, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "get()", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);
    YDB_MALLOC_BUFFER(&ret_value, 1024);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_get_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_value);

        /* check to see if length of string was longer than 1024 is so, try again with proper length */
        if (YDB_ERR_INVSTRLEN == status) {
            return_length = ret_value.len_used;
            YDB_FREE_BUFFER(&ret_value);
            YDB_MALLOC_BUFFER(&ret_value, return_length);
            /* Call the wrapped function */
            status = ydb_get_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_value);
        }

        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;

        }
        /* Create Python object to return */
        if (!return_NULL)
            return_python_string = Py_BuildValue("y#", ret_value.buf_addr, ret_value.len_used);
    }

    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer);
    YDB_FREE_BUFFER(&ret_value);
    if (return_NULL)
        return NULL;
    else
        return return_python_string;
}

/* Wrapper for ydb_incr_s() and ydb_incr_st() */
static PyObject* incr(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int status, subs_used, varname_len, increment_len;
    uint64_t tp_token;
    char *varname, *increment;
    PyObject *subsarray, *return_python_string;
    ydb_buffer_t increment_y, error_string_buffer, ret_value, varname_y, *subsarray_y;

    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;
    increment = "1";
    increment_len = 1;

    /* parse and validate */
    static char* kwlist[] = {"varname", "subsarray", "increment", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|Oy#K", kwlist, &varname, &varname_len, &subsarray,
                                        &increment, &increment_len, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "incr() for varname", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    SETUP_BUFFER(increment, increment_y, increment_len, "incr() for increment", return_NULL);

    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);
    YDB_MALLOC_BUFFER(&ret_value, 50);
    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_incr_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &increment_y, &ret_value);

        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }

        /* Create Python object to return */
        if (!return_NULL)
            return_python_string = Py_BuildValue("y#", ret_value.buf_addr, ret_value.len_used);
    }

    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&increment_y);
    YDB_FREE_BUFFER(&error_string_buffer);
    YDB_FREE_BUFFER(&ret_value);

    if (return_NULL)
        return NULL;
    else
        return return_python_string;
}

/* Wrapper for ydb_lock_s() and ydb_lock_st() */
static PyObject* lock(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    bool success = true;
    int len_keys, initial_arguments, number_of_arguments;
    uint64_t tp_token;
    unsigned long long timeout_nsec;
    ffi_cif call_interface;
    ffi_type *ret_type;
    PyObject *keys;
    ydb_buffer_t *error_string_buffer;
    YDBKey *keys_ydb = NULL;

    /* Defaults for non-required arguments */
    timeout_nsec = 0;
    tp_token = YDB_NOTTP;
    keys = Py_None;

    /* parse and validate */
    static char* kwlist[] = {"keys", "timeout_nsec", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OKK", kwlist, &keys, &timeout_nsec, &tp_token))
        return NULL;

    if (Py_None !=keys && !validate_py_keys_sequence_bytes(keys))
        return NULL;
    if (Py_None == keys)
        len_keys = 0;
    else
        len_keys = PySequence_Length(keys);

    /* Setup for Call */
    error_string_buffer = (ydb_buffer_t*)calloc(1, sizeof(ydb_buffer_t));
    YDB_MALLOC_BUFFER(error_string_buffer, YDB_MAX_ERRORMSG);
    if (Py_None != keys) {
        keys_ydb = (YDBKey*)calloc(len_keys, sizeof(YDBKey));
        success = convert_key_sequence_to_YDBKey_array(keys, keys_ydb);
        if (!success)
            return_NULL = true;
    }

    if (!return_NULL) {
        /* build ffi call */
        ret_type = &ffi_type_sint;
        initial_arguments = 4;

        number_of_arguments = initial_arguments + (len_keys * 3);
        ffi_type *arg_types[number_of_arguments];
        void *arg_values[number_of_arguments];
        /* ffi signature */
        arg_types[0] = &ffi_type_uint64; // tptoken
        arg_values[0] = &tp_token; // tptoken
        arg_types[1] = &ffi_type_pointer;// errstr
        arg_values[1] = &error_string_buffer; // errstr
        arg_types[2] = &ffi_type_uint64; // timout_nsec
        arg_values[2] = &timeout_nsec; // timout_nsec
        arg_types[3] = &ffi_type_sint; // namecount
        arg_values[3] = &len_keys; // namecount


        for (int i = 0; i < len_keys; i++) {
            int first = initial_arguments + 3*i;
            arg_types[first] = &ffi_type_pointer;// varname
            arg_values[first] = &keys_ydb[i].varname; // varname
            arg_types[first + 1] = &ffi_type_sint; // subs_used
            arg_values[first + 1] = &keys_ydb[i].subs_used; // subs_used
            arg_types[first + 2] = &ffi_type_pointer;// subsarray
            arg_values[first + 2] = &keys_ydb[i].subsarray;// subsarray
        }

        int status; // return value
        if (ffi_prep_cif(&call_interface, FFI_DEFAULT_ABI, number_of_arguments, ret_type, arg_types) == FFI_OK) {
            /* Call the wrapped function */
            ffi_call(&call_interface, FFI_FN(ydb_lock_st), &status, arg_values);
        } else {
            PyErr_SetString(PyExc_SystemError, "ffi_prep_cif failed ");
            return_NULL = true;
        }

        /* check for errors */
        if (YDB_LOCK_TIMEOUT == status) {
            PyErr_SetString(YDBTimeoutError, "Not able to acquire all requested locks in the specified time.");
            return_NULL = true;
        } else if (YDB_OK != status) {
            raise_YDBError(status, error_string_buffer, tp_token);
            return_NULL = true;
            return NULL;
        }
    }

    /* free allocated memory */
    YDB_FREE_BUFFER(error_string_buffer);
    free(error_string_buffer);
    free_YDBKey_array(keys_ydb, len_keys);

    if (return_NULL) {
        return NULL;
    } else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}

/* Wrapper for ydb_lock_decr_s() and ydb_lock_decr_st() */
static PyObject* lock_decr(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int status, varname_len, subs_used;
    char *varname;
    uint64_t tp_token;
    PyObject *subsarray;
    ydb_buffer_t error_string_buffer, varname_y, *subsarray_y;

    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char* kwlist[] = {"varname", "subsarray", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|OK", kwlist, &varname, &varname_len, &subsarray, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "lock_decr()", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_lock_decr_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y);

        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }
    }

    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer);

    if (return_NULL) {
        return NULL;
    } else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}

/* Wrapper for ydb_lock_incr_s() and ydb_lock_incr_st() */
static PyObject* lock_incr(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int status, varname_len, subs_used;
    char *varname;
    uint64_t tp_token;
    unsigned long long timeout_nsec;
    PyObject *subsarray;
    ydb_buffer_t error_string_buffer, varname_y, *subsarray_y;

    /* Defaults for non-required arguments */
    subsarray = Py_None;
    timeout_nsec = 0;
    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char* kwlist[] = {"varname", "subsarray","timeout_nsec",  "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|OLK", kwlist, &varname, &varname_len, &subsarray, &timeout_nsec, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;
    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "lock_incr()", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_lock_incr_st(tp_token, &error_string_buffer, timeout_nsec, &varname_y, subs_used, subsarray_y);

        /* check status for Errors and Raise Exception */
        if (YDB_LOCK_TIMEOUT == status) {
            PyErr_SetString(YDBTimeoutError, "Not able to acquire all requested locks in the specified time.");
            return_NULL = true;
        } else if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }
    }

    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer);

    if (return_NULL) {
        return NULL;
    } else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}

/* Wrapper for ydb_node_next_s() and ydb_node_next_st() */
static PyObject* node_next(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int max_subscript_string, default_ret_subs_used, real_ret_subs_used, ret_subs_used, status, varname_len, subs_used;
    char *varname;
    uint64_t tp_token;
    PyObject *subsarray, *return_tuple;
    ydb_buffer_t error_string_buffer, *ret_subsarray, varname_y, *subsarray_y;

    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char* kwlist[] = {"varname", "subsarray", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|OK", kwlist, &varname, &varname_len, &subsarray, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "node_next()", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);
    max_subscript_string = 1024;
    default_ret_subs_used = subs_used + 5;
    if (YDB_MAX_SUBS < default_ret_subs_used)
        default_ret_subs_used = YDB_MAX_SUBS;
    real_ret_subs_used = default_ret_subs_used;
    ret_subs_used = default_ret_subs_used;
    ret_subsarray = empty_buffer_array(ret_subs_used, max_subscript_string);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_node_next_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_subs_used, ret_subsarray);

        /* If not enough buffers in ret_subsarray */
        if (YDB_ERR_INSUFFSUBS == status) {
            FREE_BUFFER_ARRAY(ret_subsarray, default_ret_subs_used);
            real_ret_subs_used = ret_subs_used;
            ret_subsarray = empty_buffer_array(real_ret_subs_used, max_subscript_string);
            /* recall the wrapped function */
            status = ydb_node_next_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_subs_used, ret_subsarray);
        }

        /* if a buffer is not long enough */
        while(YDB_ERR_INVSTRLEN == status) {
            max_subscript_string = ret_subsarray[ret_subs_used].len_used;
            YDB_FREE_BUFFER(&ret_subsarray[ret_subs_used])
            YDB_MALLOC_BUFFER(&ret_subsarray[ret_subs_used], max_subscript_string);
            ret_subs_used = real_ret_subs_used;
            /* recall the wrapped function */
            status = ydb_node_next_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_subs_used, ret_subsarray);
        }

        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }
        /* Create Python object to return */
        if (!return_NULL)
            return_tuple = convert_ydb_buffer_array_to_py_tuple(ret_subsarray, ret_subs_used);
    }
    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer);
    FREE_BUFFER_ARRAY(ret_subsarray, real_ret_subs_used);

    if (return_NULL)
        return NULL;
    else
        return return_tuple;
}

/* Wrapper for ydb_node_previous_s() and ydb_node_previous_st() */
static PyObject* node_previous(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int max_subscript_string, default_ret_subs_used, real_ret_subs_used, ret_subs_used, status, varname_len, subs_used;
    char *varname;
    uint64_t tp_token;
    PyObject *subsarray, *return_tuple;
    ydb_buffer_t error_string_buffer, *ret_subsarray, varname_y, *subsarray_y;


    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char* kwlist[] = {"varname", "subsarray", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|OK", kwlist, &varname, &varname_len, &subsarray, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "node_previous()", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);

    max_subscript_string = 1024;
    default_ret_subs_used = subs_used - 1;
    if (0 >= default_ret_subs_used)
        default_ret_subs_used = 1;
    real_ret_subs_used = default_ret_subs_used;
    ret_subs_used = default_ret_subs_used;
    ret_subsarray = empty_buffer_array(ret_subs_used, max_subscript_string);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_node_previous_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_subs_used, ret_subsarray);

        /* if a buffer is not long enough */
        while(YDB_ERR_INVSTRLEN == status) {
            max_subscript_string = ret_subsarray[ret_subs_used].len_used;
            YDB_FREE_BUFFER(&ret_subsarray[ret_subs_used])
            YDB_MALLOC_BUFFER(&ret_subsarray[ret_subs_used], max_subscript_string);
            ret_subs_used = real_ret_subs_used;
            /* recall the wrapped function */
            status = ydb_node_previous_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y,
                                          &ret_subs_used, ret_subsarray);
        }
        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }

        /* Create Python object to return */
        if (!return_NULL)
            return_tuple = convert_ydb_buffer_array_to_py_tuple(ret_subsarray, ret_subs_used);
    }

    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer);
    FREE_BUFFER_ARRAY(ret_subsarray, real_ret_subs_used);

    if (return_NULL)
        return NULL;
    else
        return return_tuple;
}

/* Wrapper for ydb_set_s() and ydb_set_st() */
static PyObject* set(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int status, varname_len, value_len, subs_used;
    uint64_t tp_token;
    char *varname, *value;
    PyObject *subsarray;
    ydb_buffer_t error_string_buffer, value_buffer, varname_y, *subsarray_y;


    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;
    value = "";

    /* parse and validate */
    static char* kwlist[] = {"varname", "subsarray", "value", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|Oy#K", kwlist, &varname, &varname_len, &subsarray,
                                     &value, &value_len, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "set() for varname", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);
    SETUP_BUFFER(value, value_buffer, value_len, "set() for value", return_NULL);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_set_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &value_buffer);

        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }
    }

    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    YDB_FREE_BUFFER(&value_buffer);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer);

    if (return_NULL) {
        return NULL;
    } else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}

/* Wrapper for ydb_str2zwr_s() and ydb_str2zwr_st() */
static PyObject* str2zwr(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int str_len, status, return_length;
    uint64_t tp_token;
    char *str;
    ydb_buffer_t error_string_buf, str_buf, zwr_buf;
    PyObject* return_value;

    /* Defaults for non-required arguments */
    str = "";
    str_len = 0;
    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char* kwlist[] = {"input", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|K", kwlist, &str, &str_len, &tp_token))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(str, str_buf, str_len, "ydb_str2zwr", return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buf, YDB_MAX_ERRORMSG);
    YDB_MALLOC_BUFFER(&zwr_buf, 1024);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_str2zwr_st(tp_token, &error_string_buf, &str_buf, &zwr_buf);


        /* recall with properly sized buffer if zwr_buf is not long enough */
        if (YDB_ERR_INVSTRLEN == status) {
            return_length = zwr_buf.len_used;
            YDB_FREE_BUFFER(&zwr_buf);
            YDB_MALLOC_BUFFER(&zwr_buf, return_length);
            /* recall the wrapped function */
            status = ydb_str2zwr_st(tp_token, &error_string_buf, &str_buf, &zwr_buf);
        }

        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buf, tp_token);
            return_NULL = true;
        }

        /* Create Python object to return */
        if (!return_NULL)
            return_value =  Py_BuildValue("y#", zwr_buf.buf_addr, zwr_buf.len_used);
    }
    /* free allocated memory */
    YDB_FREE_BUFFER(&str_buf);
    YDB_FREE_BUFFER(&error_string_buf);
    YDB_FREE_BUFFER(&zwr_buf);

    if (return_NULL)
        return NULL;
    else
        return return_value;
}

/* Wrapper for ydb_subscript_next_s() and ydb_subscript_next_st() */
static PyObject* subscript_next(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int status, return_length, varname_len, subs_used;
    char *varname;
    uint64_t tp_token;
    PyObject *subsarray, *return_python_string;
    ydb_buffer_t error_string_buffer, ret_value, varname_y, *subsarray_y;

    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;

    /* parse and validate */
        static char* kwlist[] = {"varname", "subsarray", "tp_token", NULL};
        if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|OK", kwlist, &varname, &varname_len, &subsarray, &tp_token))
            return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "subscript_next()", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);
    YDB_MALLOC_BUFFER(&ret_value, 1024);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_subscript_next_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_value);

        /* check to see if length of string was longer than 1024 is so, try again with proper length */
        if (YDB_ERR_INVSTRLEN == status) {
            return_length = ret_value.len_used;
            YDB_FREE_BUFFER(&ret_value);
            YDB_MALLOC_BUFFER(&ret_value, return_length);
            /* recall the wrapped function */
            status = ydb_subscript_next_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_value);
        }
        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }

        /* Create Python object to return */
        if (!return_NULL)
            return_python_string = Py_BuildValue("y#", ret_value.buf_addr, ret_value.len_used);
    }
    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer);
    YDB_FREE_BUFFER(&ret_value);

    if (return_NULL)
        return NULL;
    else
        return return_python_string;
}

/* Wrapper for ydb_subscript_previous_s() and ydb_subscript_previous_st() */
static PyObject* subscript_previous(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int status, return_length, varname_len, subs_used;
    char *varname;
    uint64_t tp_token;
    PyObject *subsarray, *return_python_string;
    ydb_buffer_t error_string_buffer, ret_value, varname_y, *subsarray_y;

    /* Defaults for non-required arguments */
    subsarray = Py_None;
    tp_token = YDB_NOTTP;

    /* Setup for Call */
    static char* kwlist[] = {"varname", "subsarray", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|OK", kwlist, &varname, &varname_len, &subsarray, &tp_token))
        return NULL;

    if (!validate_subsarray_object(subsarray))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(varname, varname_y, varname_len, "subscript_previous()", return_NULL);
    SETUP_SUBS(subsarray, subs_used, subsarray_y, return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);
    YDB_MALLOC_BUFFER(&ret_value, 1024);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_subscript_previous_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_value);

        /* check to see if length of string was longer than 1024 is so, try again with proper length */
        if (YDB_ERR_INVSTRLEN == status) {
            return_length = ret_value.len_used;
            YDB_FREE_BUFFER(&ret_value);
            YDB_MALLOC_BUFFER(&ret_value, return_length);
            status = ydb_subscript_previous_st(tp_token, &error_string_buffer, &varname_y, subs_used, subsarray_y, &ret_value);
        }

        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }

        /* Create Python object to return */
        if (!return_NULL)
            return_python_string = Py_BuildValue("y#", ret_value.buf_addr, ret_value.len_used);
    }
    /* free allocated memory */
    YDB_FREE_BUFFER(&varname_y);
    FREE_BUFFER_ARRAY(subsarray_y, subs_used);
    YDB_FREE_BUFFER(&error_string_buffer);
    YDB_FREE_BUFFER(&ret_value);
    if (return_NULL)
        return NULL;
    else
        return return_python_string;
}

/* Callback functions used by Wrapper for ydb_tp_s() / ydb_tp_st() */

/* Callback Wrapper used by tp_st. The aproach of calling a Python function is a bit of a hack. Here's how it works:
 *    1) This is the callback function always the function passed to called by ydb_tp_st.
 *    2) the actual Python function to be called is passed to this function as the first element in a Python tuple.
 *    3) the positional arguments are passed as the second element and the keyword args are passed as the third.
 *    4) the new tp_token that ydb_tp_st passes to this function as an argument is added to the kwargs dictionary.
 *    5) this function calls calls the python callback function with the args and kwargs arguments.
 *    6) if a function raises an exception then this function returns TEMP_YDB_RAISE_PYTHON_EXCEPTION as a way of indicating an error.
 *            TODO: replace TEMP_YDB_RAISE_PYTHON_EXCEPTION after resolution of YDB issue #548
 *            (note) the PyErr String is already set so the the function receiving the return value (tp) just needs to return NULL.
 */
static int callback_wrapper(uint64_t tp_token, ydb_buffer_t*errstr, void *function_with_arguments) {
    /* this should only ever be called by ydb_tp_st c api via tp below.
     * It assumes that everything passed to it was validated.
     */
    int return_val;
    bool decref_args = false;
    bool decref_kwargs = false;
    PyObject *function, *args, *kwargs, *return_value, *tp_token_py;


    function = PyTuple_GetItem(function_with_arguments, 0);
    args = PyTuple_GetItem(function_with_arguments, 1);
    kwargs = PyTuple_GetItem(function_with_arguments, 2);

    if (Py_None == args) {
        args = PyTuple_New(0);
        decref_args = true;
    }
    if (Py_None == kwargs) {
        kwargs = PyDict_New();
        decref_kwargs = true;
    }

    tp_token_py = Py_BuildValue("K", tp_token);
    PyDict_SetItemString(kwargs, "tp_token", tp_token_py);
    Py_DECREF(tp_token_py);

    return_value = PyObject_Call(function, args, kwargs);

    if (decref_args)
        Py_DECREF(args);
    if (decref_kwargs)
        Py_DECREF(kwargs);

    if (NULL == return_value) {
        /* function raised an exception */
        return TEMP_YDB_RAISE_PYTHON_EXCEPTION; // TODO: replace after resolution of YDB issue #548
    } else if (!PyLong_Check(return_value)){
        PyErr_SetString(PyExc_TypeError, "Callback function must return value of type int.");
        return TEMP_YDB_RAISE_PYTHON_EXCEPTION; // TODO: replace after resolution of YDB issue #548
    }
    return_val = (int)PyLong_AsLong(return_value);
    Py_DECREF(return_value);
    return return_val;
}

/* Wrapper for ydb_tp_s() / ydb_tp_st() */
static PyObject* tp(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    bool success;
    int namecount, status;
    uint64_t tp_token;
    char *transid;
    PyObject *callback, *callback_args, *callback_kwargs, *varnames, *function_with_arguments;
    ydb_buffer_t error_string_buffer, *varname_buffers;

    /* Defaults for non-required arguments */
    callback_args = Py_None;
    callback_kwargs = Py_None;
    transid = "BATCH";
    namecount = 0;
    varnames = Py_None;

    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char *kwlist[] = {"callback", "args", "kwargs", "transid", "varnames", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOsOK", kwlist, &callback, &callback_args, &callback_kwargs, &transid, &varnames, &tp_token))
        return_NULL = true;

    /* validate input */
    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "'callback' must be a callable.");
        return_NULL = true;
    }
    if (Py_None != callback_args && !PyTuple_Check(callback_args)) {
        PyErr_SetString(PyExc_TypeError, "'args' must be a tuple. "
                                        "(It will be passed to the callback function as positional arguments.)");
        return_NULL = true;
    }
    if (Py_None != callback_kwargs && !PyDict_Check(callback_kwargs)) {
        PyErr_SetString(PyExc_TypeError, "'kwargs' must be a dictionary. "
                                        "(It will be passed to the callback function as keyword arguments.)");
        return_NULL = true;
    }

    if(Py_None != varnames && !validate_sequence_of_bytes(varnames)) {
        PyErr_SetString(PyExc_TypeError, "'varnames' must be a sequence of bytes. ");
        return_NULL = true;
    }

    if (!return_NULL) {
        /* Setup for Call */
        YDB_MALLOC_BUFFER(&error_string_buffer, YDB_MAX_ERRORMSG);
        function_with_arguments = Py_BuildValue("(OOO)", callback, callback_args, callback_kwargs);
        namecount = 0;
        if (Py_None != varnames)
            namecount = PySequence_Length(varnames);

        varname_buffers = (ydb_buffer_t*)calloc(namecount, sizeof(ydb_buffer_t));
        if (0 < namecount) {
            success = convert_py_bytes_sequence_to_ydb_buffer_array(varnames, namecount, varname_buffers);
            if (!success)
                return NULL;
        }

        /* Call the wrapped function */
        status = ydb_tp_st(tp_token, &error_string_buffer, callback_wrapper, function_with_arguments, transid,
                            namecount, varname_buffers);

        /* check status for Errors and Raise Exception */
        if (TEMP_YDB_RAISE_PYTHON_EXCEPTION == status) { // TODO: replace after resolution of YDB issue #548
            return_NULL = true;
        } else if (YDB_TP_RESTART == status) {
            PyErr_SetString(YDBTPRestart, "tp() callback function returned 'YDB_TP_RESTART'.");
            return_NULL = true;
        } else if (YDB_TP_ROLLBACK == status) {
            PyErr_SetString(YDBTPRollback, "tp() callback function returned 'YDB_TP_ROLLBACK'.");
            return_NULL = true;

        } else if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buffer, tp_token);
            return_NULL = true;
        }
        /* free allocated memory */
        YDB_FREE_BUFFER(&error_string_buffer);
        free(varname_buffers);
    }

    if (return_NULL)
        return NULL;
    else
        return Py_BuildValue("i", status);
}

/* Wrapper for ydb_zwr2str_s() and ydb_zwr2str_st() */
static PyObject* zwr2str(PyObject* self, PyObject* args, PyObject *kwds) {
    bool return_NULL = false;
    int zwr_len, status, return_length;
    uint64_t tp_token;
    char *zwr;
    PyObject *return_value;
    ydb_buffer_t error_string_buf, zwr_buf, str_buf;

    /* Defaults for non-required arguments */
    zwr = "";
    zwr_len = 0;
    tp_token = YDB_NOTTP;

    /* parse and validate */
    static char* kwlist[] = {"input", "tp_token", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|K", kwlist, &zwr, &zwr_len, &tp_token))
        return NULL;

    /* Setup for Call */
    SETUP_BUFFER(zwr, zwr_buf, zwr_len, "zwr2str()", return_NULL);
    YDB_MALLOC_BUFFER(&error_string_buf, YDB_MAX_ERRORMSG);
    YDB_MALLOC_BUFFER(&str_buf, 1024);

    if (!return_NULL) {
        /* Call the wrapped function */
        status = ydb_zwr2str_st(tp_token, &error_string_buf, &zwr_buf, &str_buf);

        /* recall with properly sized buffer if zwr_buf is not long enough */
        if (YDB_ERR_INVSTRLEN == status) {
            return_length = str_buf.len_used;
            YDB_FREE_BUFFER(&str_buf);
            YDB_MALLOC_BUFFER(&str_buf, return_length);
            /* recall the wrapped function */
            status = ydb_zwr2str_st(tp_token, &error_string_buf, &zwr_buf, &str_buf);
        }

        /* check status for Errors and Raise Exception */
        if (YDB_OK != status) {
            raise_YDBError(status, &error_string_buf, tp_token);
            return_NULL = true;
        }

        if (!return_NULL)
            return_value =  Py_BuildValue("y#", str_buf.buf_addr, str_buf.len_used);
    }
    YDB_FREE_BUFFER(&zwr_buf);
    YDB_FREE_BUFFER(&error_string_buf);
    YDB_FREE_BUFFER(&str_buf);

    if (return_NULL)
        return NULL;
    else
        return return_value;
}

/*Comprehensive API
 *Utility Functions
 *
 *    ydb_child_init()
 *    ydb_exit()
 *    ydb_file_id_free() / ydb_file_id_free_t()
 *    ydb_file_is_identical() / ydb_file_is_identical_t()
 *    ydb_file_name_to_id() / ydb_file_name_to_id_t()
 *    ydb_fork_n_core()
 *    ydb_free()
 *    ydb_hiber_start()
 *    ydb_hiber_start_wait_any()
 *    ydb_init()
 *    ydb_malloc()
 *    ydb_message() / ydb_message_t()
 *    ydb_stdout_stderr_adjust() / ydb_stdout_stderr_adjust_t()
 *    ydb_thread_is_main()
 *    ydb_timer_cancel() / ydb_timer_cancel_t()
 *    ydb_timer_start() / ydb_timer_start_t()

 *Calling M Routines
 */



/* Pull everything together into a Python Module */
static PyMethodDef methods[] = {
    /* Simple and Simple Threaded API Functions */
    {"data", (PyCFunction)data, METH_VARARGS | METH_KEYWORDS, "used to learn what type of data is at a node.\n "
                                                                        "0 : There is neither a value nor a subtree, "
                                                                        "i.e., it is undefined.\n"
                                                                        "1 : There is a value, but no subtree\n"
                                                                        "10 : There is no value, but there is a subtree.\n"
                                                                        "11 : There are both a value and a subtree.\n"},
    {"delete", (PyCFunction)delete_wrapper, METH_VARARGS | METH_KEYWORDS, "deletes node value or tree data at node"},
    {"delete_excel", (PyCFunction)delete_excel, METH_VARARGS | METH_KEYWORDS, "delete the trees of all local variables "
                                                                                    "except those in the 'varnames' array"},
    {"get", (PyCFunction)get, METH_VARARGS | METH_KEYWORDS, "returns the value of a node or raises exception"},
    {"incr", (PyCFunction)incr, METH_VARARGS | METH_KEYWORDS, "increments value by the value specified by 'increment'"},

    {"lock", (PyCFunction)lock, METH_VARARGS | METH_KEYWORDS, "..."},

    {"lock_decr", (PyCFunction)lock_decr, METH_VARARGS | METH_KEYWORDS, "Decrements the count of the specified lock held "
                                                                            "by the process. As noted in the Concepts section, a "
                                                                            "lock whose count goes from 1 to 0 is released. A lock "
                                                                            "whose name is specified, but which the process does "
                                                                            "not hold, is ignored."},
    {"lock_incr", (PyCFunction)lock_incr, METH_VARARGS | METH_KEYWORDS, "Without releasing any locks held by the process, "
                                                                            "attempt to acquire the requested lock incrementing it"
                                                                            " if already held."},
    {"node_next", (PyCFunction)node_next, METH_VARARGS | METH_KEYWORDS, "facilitate depth-first traversal of a local or global"
                                                                            " variable tree. returns string tuple of subscripts of"
                                                                            " next node with value."},
    {"node_previous", (PyCFunction)node_previous, METH_VARARGS | METH_KEYWORDS, "facilitate depth-first traversal of a local "
                                                                                    "or global variable tree. returns string tuple"
                                                                                    "of subscripts of previous node with value."},
    {"set", (PyCFunction)set, METH_VARARGS | METH_KEYWORDS, "sets the value of a node or raises exception"},
    {"str2zwr", (PyCFunction)str2zwr, METH_VARARGS | METH_KEYWORDS, "returns the zwrite formatted (Bytes Object) version of the"
                                                                        " Bytes object provided as input."},
    {"subscript_next", (PyCFunction)subscript_next, METH_VARARGS | METH_KEYWORDS, "returns the name of the next subscript at "
                                                                                      "the same level as the one given"},
    {"subscript_previous", (PyCFunction)subscript_previous, METH_VARARGS | METH_KEYWORDS, "returns the name of the previous "
                                                                                              "subscript at the same level as the "
                                                                                              "one given"},
    {"tp", (PyCFunction)tp, METH_VARARGS | METH_KEYWORDS, "transaction"},

    {"zwr2str", (PyCFunction)zwr2str, METH_VARARGS | METH_KEYWORDS, "returns the Bytes Object from the zwrite formated Bytes "
                                                                        "object provided as input."},
    /* API Utility Functions */
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef _yottadbmodule = {
    PyModuleDef_HEAD_INIT,
    "_yottadb",   /* name of module */
    "A module that provides basic access to YottaDB's c api", /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
    methods
};

PyMODINIT_FUNC PyInit__yottadb(void) {
    PyObject *module = PyModule_Create(&_yottadbmodule);
    if (NULL == module)
        return NULL;

    /* Defining Module 'Constants' */
    PyObject *module_dictionary = PyModule_GetDict(module);

    /* expose constants defined in c */
    PyDict_SetItemString(module_dictionary, "YDB_DEL_TREE", Py_BuildValue("i", YDB_DEL_TREE));
    PyDict_SetItemString(module_dictionary, "YDB_DEL_NODE", Py_BuildValue("i", YDB_DEL_NODE));

    PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_WARNING", Py_BuildValue("i", YDB_SEVERITY_WARNING));
    PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_SUCCESS", Py_BuildValue("i", YDB_SEVERITY_SUCCESS));
    PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_ERROR", Py_BuildValue("i", YDB_SEVERITY_ERROR));
    PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_INFORMATIONAL", Py_BuildValue("i", YDB_SEVERITY_INFORMATIONAL));
    PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_FATAL", Py_BuildValue("i", YDB_SEVERITY_FATAL));

    PyDict_SetItemString(module_dictionary, "YDB_DATA_UNDEF", Py_BuildValue("i", YDB_DATA_UNDEF));
    PyDict_SetItemString(module_dictionary, "YDB_DATA_VALUE_NODESC", Py_BuildValue("i", YDB_DATA_VALUE_NODESC));
    PyDict_SetItemString(module_dictionary, "YDB_DATA_NOVALUE_DESC", Py_BuildValue("i", YDB_DATA_NOVALUE_DESC));
    PyDict_SetItemString(module_dictionary, "YDB_DATA_VALUE_DESC", Py_BuildValue("i", YDB_DATA_VALUE_DESC));
    PyDict_SetItemString(module_dictionary, "YDB_DATA_ERROR", Py_BuildValue("i", YDB_DATA_ERROR));

    PyDict_SetItemString(module_dictionary, "YDB_RELEASE", Py_BuildValue("i", YDB_RELEASE));

    PyDict_SetItemString(module_dictionary, "YDB_MAX_IDENT", Py_BuildValue("i", YDB_MAX_IDENT));
    PyDict_SetItemString(module_dictionary, "YDB_MAX_NAMES", Py_BuildValue("i", YDB_MAX_NAMES));
    PyDict_SetItemString(module_dictionary, "YDB_MAX_STR", Py_BuildValue("i", YDB_MAX_STR));
    PyDict_SetItemString(module_dictionary, "YDB_MAX_SUBS", Py_BuildValue("i", YDB_MAX_SUBS));
    PyDict_SetItemString(module_dictionary, "YDB_MAX_TIME_NSEC", Py_BuildValue("L", YDB_MAX_TIME_NSEC));
    PyDict_SetItemString(module_dictionary, "YDB_MAX_YDBERR", Py_BuildValue("i", YDB_MAX_YDBERR));
    PyDict_SetItemString(module_dictionary, "YDB_MAX_ERRORMSG", Py_BuildValue("i", YDB_MAX_ERRORMSG));

    PyDict_SetItemString(module_dictionary, "YDB_MIN_YDBERR", Py_BuildValue("i", YDB_MIN_YDBERR));

    PyDict_SetItemString(module_dictionary, "YDB_OK", Py_BuildValue("i", YDB_OK));

    PyDict_SetItemString(module_dictionary, "YDB_INT_MAX", Py_BuildValue("i", YDB_INT_MAX));
    PyDict_SetItemString(module_dictionary, "YDB_TP_RESTART", Py_BuildValue("i", YDB_TP_RESTART));
    PyDict_SetItemString(module_dictionary, "YDB_TP_ROLLBACK", Py_BuildValue("i", YDB_TP_ROLLBACK));
    PyDict_SetItemString(module_dictionary, "YDB_NOTOK", Py_BuildValue("i", YDB_NOTOK));
    PyDict_SetItemString(module_dictionary, "YDB_LOCK_TIMEOUT", Py_BuildValue("i", YDB_LOCK_TIMEOUT));

    PyDict_SetItemString(module_dictionary, "YDB_NOTTP", Py_BuildValue("i", YDB_NOTTP));

    PyDict_SetItemString(module_dictionary, "YDB_ERR_TPTIMEOUT", Py_BuildValue("i", YDB_ERR_TPTIMEOUT));



    /* Exceptions */
    YDBException = PyErr_NewException("_yottadb.YDBException",
                                        NULL, // use to pick base class
                                        NULL);
    PyModule_AddObject(module,"YDBException", YDBException);

    YDBTPException = PyErr_NewException("_yottadb.YDBTPException",
                                        YDBException, // use to pick base class
                                        NULL);
    PyModule_AddObject(module,"YDBTPException", YDBTPException);

    YDBTPRollback = PyErr_NewException("_yottadb.YDBTPRollback",
                                        YDBTPException, // use to pick base class
                                        NULL);
    PyModule_AddObject(module,"YDBTPRollback", YDBTPRollback);

    YDBTPRestart = PyErr_NewException("_yottadb.YDBTPRestart",
                                        YDBTPException, // use to pick base class
                                        NULL);
    PyModule_AddObject(module,"YDBTPRestart", YDBTPRestart);


    /* setting up YDBTimeoutError */
    YDBTimeoutError = PyErr_NewException("_yottadb.YDBTimeoutError",
                                        YDBException, // use to pick base class
                                        NULL);
    PyModule_AddObject(module,"YDBTimeoutError", YDBTimeoutError);

    /* setting up YDBPythonError */
    YDBPythonError = PyErr_NewException("_yottadb.YDBPythonError",
                                        YDBException, // use to pick base class
                                        NULL);
    PyModule_AddObject(module,"YDBPythonError", YDBPythonError);

    YDBError = PyErr_NewException("_yottadb.YDBError",
                                        YDBException, // use to pick base class
                                        NULL);
    PyModule_AddObject(module,"YDBError", YDBError);

    ADD_YDBERRORS();
    return module;
}