/****************************************************************
 *                                                              *
 * Copyright (c) 2019-2021 Peter Goss All rights reserved.      *
 *                                                              *
 * Copyright (c) 2019-2021 YottaDB LLC and/or its subsidiaries. *
 * All rights reserved.                                         *
 *                                                              *
 *  This source code contains the intellectual property         *
 *  of its copyright holder(s), and is made available           *
 *  under a license.  If you do not know the terms of           *
 *  the license, please stop and do not read further.           *
 *                                                              *
 ****************************************************************/

#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <pthread.h>
#include <libyottadb.h>
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "_yottadb.h"
#include "_yottadbexceptions.h"

#define DECREF_AND_RETURN(PY_OBJECT, RET)     \
	{                                     \
		if (NULL != PY_OBJECT) {      \
			Py_DECREF(PY_OBJECT); \
		}                             \
		return RET;                   \
	}

/* Local Utility Functions */
/* Routine to create an array of empty ydb_buffer_ts with num elements each with
 * an allocated length of len
 *
 * Parameters:
 *   num    - the number of buffers to allocate in the array
 *   len    - the length of the string to allocate for each of the the ydb_buffer_ts
 *
 * free with FREE_BUFFER_ARRAY macro
 */
static ydb_buffer_t *create_empty_buffer_array(int num, int len) {
	int	      i;
	ydb_buffer_t *return_buffer_array;

	return_buffer_array = (ydb_buffer_t *)calloc(num, sizeof(ydb_buffer_t));
	for (i = 0; i < num; i++)
		YDB_MALLOC_BUFFER(&return_buffer_array[i], len);

	return return_buffer_array;
}

/* Routine that raises the appropriate Python Error during validation of Python input
 * parameters. The 2 types of errors that will be raised are:
 *     1) TypeError in the case that the parameter is of the wrong type
 *     2) ValueError if the parameter is of the right type but is invalid in some other way (e.g. too long)
 *
 * Parameters:
 *     status  - the error message status number (specific values defined in _yottdb.h)
 *     message - the message to be set in the Python exception.
 */
static void raise_ValidationError(YDBPythonErrorType err_type, char *format_prefix, char *err_format, ...) {
	va_list args;
	char	err_message[YDBPY_MAX_ERRORMSG];
	char	prefixed_err_message[YDBPY_MAX_ERRORMSG];
	int	copied;

	va_start(args, err_format);
	copied = snprintf(err_message, YDBPY_MAX_ERRORMSG, err_format, args);
	assert(YDBPY_MAX_ERRORMSG > copied);
	UNUSED(copied);
	va_end(args);

	if (NULL != format_prefix) {
		copied = snprintf(prefixed_err_message, YDBPY_MAX_ERRORMSG, format_prefix, err_message);
	} else {
		copied = snprintf(prefixed_err_message, YDBPY_MAX_ERRORMSG, "%s", err_message);
	}
	assert(0 <= copied);
	if (YDBPY_MAX_ERRORMSG <= copied) {
		char * ellipsis = "...";
		size_t ellipsis_len;

		ellipsis_len = strlen(ellipsis);
		memcpy(&prefixed_err_message[YDBPY_MAX_ERRORMSG - (ellipsis_len + 1)], ellipsis, ellipsis_len);
		assert('\0' == prefixed_err_message[YDBPY_MAX_ERRORMSG - 1]);
	}

	switch (err_type) {
	case YDBPython_TypeError:
		PyErr_SetString(PyExc_TypeError, err_message);
		break;
	case YDBPython_ValueError:
		PyErr_SetString(PyExc_ValueError, err_message);
		break;
	default:
		// Only TypeError and ValueError are possible, so we should never get here.
		assert(FALSE);
		break;
	}
}

/* Conversion Utilities */

/* Confirm that the passed PyObject is a valid Python Sequence,
 * i.e. a Sequence of Python `str` (i.e. Unicode) objects.
 *
 * Validation and error reporting will vary depending on the type of Sequence
 * to be validated.
 *
 * Note: If YDBPython API calls are to accept Python `bytes` objects in
 * addition to `str` objects, this function will need to be updated.
 *
 * Parameters:
 *   object        - the Python object to check
 *   sequence_type - the type of Sequence to check for
 */
static bool is_valid_sequence(PyObject *object, YDBPythonSequenceType sequence_type, char *extra_prefix) {
	int	   max_sequence_len, max_item_len;
	Py_ssize_t i, sequence_len, item_len;
	PyObject * item, *sequence;
	char *	   err_prefix;

	if (Py_None == object) {
		// Allow Python None type
		return TRUE;
	}

	/* Different checks will need to be performed and error messages issued
	 * depending on the type of Sequence we are validating. So, check that
	 * here and initialize relevant variables accordingly.
	 */
	switch (sequence_type) {
	case YDBPython_VarnameSequence:
		max_sequence_len = YDB_MAX_NAMES;
		max_item_len = YDB_MAX_IDENT;
		err_prefix = YDBPY_ERR_VARNAME_INVALID;
		break;
	case YDBPython_SubsarraySequence:
		max_sequence_len = YDB_MAX_SUBS;
		max_item_len = YDB_MAX_STR;
		err_prefix = YDBPY_ERR_SUBSARRAY_INVALID;
		break;
	case YDBPython_KeySequence:
		max_sequence_len = YDB_MAX_SUBS;
		max_item_len = YDB_MAX_STR;
		/* Aggregate multiple levels of error message nesting into single prefix.
		 * This allows for failures in Sequences of Keys to be tracked through the
		 * multiple levels of indirection implicated in that case. Specifically, this
		 * allows error messages to report which item in the Key Sequence is faulty, as
		 * well as which item within the Key itself is faulty.
		 */
		err_prefix = extra_prefix;
		break;
	default:
		assert(FALSE);
		break;
	}

	/* Confirm object is a Sequence */
	sequence = PySequence_Fast(object, "argument must be iterable"); // New Reference
	if (!sequence || !(PyTuple_Check(object) || PyList_Check(object))) {
		raise_ValidationError(YDBPython_TypeError, err_prefix, YDBPY_ERR_NOT_LIST_OR_TUPLE);
		if (sequence) {
			DECREF_AND_RETURN(sequence, FALSE);
		} else {
			return FALSE;
		}
	}
	/* Validate Sequence length */
	sequence_len = PySequence_Fast_GET_SIZE(sequence);
	if (max_sequence_len < sequence_len) {
		raise_ValidationError(YDBPython_ValueError, err_prefix, YDBPY_ERR_SEQUENCE_TOO_LONG, sequence_len,
				      max_sequence_len);
		DECREF_AND_RETURN(sequence, FALSE);
	}

	/* Validate Sequence contents */
	for (i = 0; i < sequence_len; i++) {
		item = PySequence_Fast_GET_ITEM(sequence, i); // Borrowed Reference
		/* Validate item type (str or bytes) */
		if (PyUnicode_Check(item)) {
			// Get length of Unicode object by multiplying code points by code point size
			item_len = PyUnicode_GET_LENGTH(item) * PyUnicode_KIND(item);
		} else if (PyBytes_Check(item)) {
			item_len = PyBytes_Size(item);
		} else {
			raise_ValidationError(YDBPython_TypeError, err_prefix, YDBPY_ERR_ITEM_NOT_BYTES_LIKE, i);
			DECREF_AND_RETURN(sequence, FALSE);
		}
		// item_len = PySequence_Fast_GET_SIZE(item);
		/* Validate item length */
		if (max_item_len < item_len) {
			raise_ValidationError(YDBPython_ValueError, err_prefix, YDBPY_ERR_BYTES_TOO_LONG, item_len, max_item_len);
			DECREF_AND_RETURN(sequence, FALSE);
		}
	}

	Py_DECREF(sequence);

	return TRUE;
}

/* Routine to convert a sequence of Python bytes objects into a C array of
 * ydb_buffer_ts. Routine assumes sequence was already validated with
 * 'is_valid_sequence' function or the special case macro
 * RETURN_IF_INVALID_SEQUENCE. The function creates a
 * copy of each Python bytes' data so the resulting array should be
 * freed by using the 'FREE_BUFFER_ARRAY' macro.
 *
 * Parameters:
 *    sequence    - a Python Object that is expected to be a Python Sequence containing Strings.
 */
bool convert_py_sequence_to_ydb_buffer_array(PyObject *sequence, int sequence_len, ydb_buffer_t *buffer_array) {
	bool	     done, free_bytes;
	Py_ssize_t   bytes_ssize;
	unsigned int bytes_len;
	char *	     bytes_c;
	PyObject *   bytes, *seq;

	seq = PySequence_Fast(sequence, "argument must be iterable"); // New Reference
	if (!seq) {
		PyErr_SetString(YDBPythonError, "Can't convert none sequence to buffer array.");
		return false;
	}

	for (int i = 0; i < sequence_len; i++) {

		bytes = PySequence_Fast_GET_ITEM(seq, i); // Borrowed Reference
		if (PyUnicode_Check(bytes)) {
			// Convert Unicode object into Python bytes object
			bytes = PyUnicode_AsEncodedString(bytes, "utf-8", "strict"); // New reference
			if (NULL == bytes) {
				FREE_BUFFER_ARRAY(buffer_array, i);
				Py_DECREF(seq);
				PyErr_SetString(YDBPythonError, "failed to encode Unicode string to bytes object");
				return false;
			}
			free_bytes = true;
		} else {
			// This should be a bytes object per validation by is_valid_sequence
			assert(PyBytes_Check(bytes));
			free_bytes = false;
		}

		// Convert Python bytes object to C character string (char *)
		bytes_ssize = PyBytes_Size(bytes);
		bytes_len = Py_SAFE_DOWNCAST(bytes_ssize, Py_ssize_t, unsigned int);
		bytes_c = PyBytes_AsString(bytes);

		YDB_MALLOC_BUFFER(&buffer_array[i], bytes_len);
		YDB_COPY_BYTES_TO_BUFFER(bytes_c, bytes_len, &buffer_array[i], done);
		if (free_bytes) {
			Py_DECREF(bytes);
		}
		if (!done) {
			FREE_BUFFER_ARRAY(buffer_array, i);
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
 *    buffer_array       - a C array of ydb_buffer_ts
 *    len                - the length of the above array
 */
PyObject *convert_ydb_buffer_array_to_py_tuple(ydb_buffer_t *buffer_array, int len) {
	int	  i;
	PyObject *return_tuple;

	return_tuple = PyTuple_New(len); // New Reference
	for (i = 0; i < len; i++)
		PyTuple_SetItem(return_tuple, i, Py_BuildValue("s#", buffer_array[i].buf_addr, buffer_array[i].len_used));

	return return_tuple;
}

/* This function will take an already allocated array of YDBKey structures and load it with the
 * data contained in the PyObject arguments.
 *
 * Parameters:
 *    dest       - pointer to the YDBKey to fill.
 *    varname    - Python bytes object representing the varname
 *    subsarray  - array of Python bytes objects representing the array of subscripts
 *                   Note: Because this function calls `convert_py_sequence_to_ydb_buffer_array`
 *                          subsarray should be validated with the
 *                          RETURN_IF_INVALID_SEQUENCE macro.
 */
static bool load_YDBKey(YDBKey *dest, PyObject *varname, PyObject *subsarray) {
	bool	      done, convert_success;
	Py_ssize_t    len_ssize, sequence_len_ssize;
	unsigned int  len;
	char *	      bytes_c;
	ydb_buffer_t *varname_y, *subsarray_y;

	if (!PyBytes_Check(varname)) {
		// Convert Unicode object to bytes object
		varname = PyUnicode_AsEncodedString(varname, "utf-8", "strict"); // New reference
		assert(PyBytes_Check(varname));
	}

	len_ssize = PyBytes_Size(varname);
	len = Py_SAFE_DOWNCAST(len_ssize, Py_ssize_t, unsigned int);
	bytes_c = PyBytes_AsString(varname);

	varname_y = (ydb_buffer_t *)calloc(1, sizeof(ydb_buffer_t));
	YDB_MALLOC_BUFFER(varname_y, len);
	YDB_COPY_BYTES_TO_BUFFER(bytes_c, len, varname_y, done);
	if (!done) {
		YDB_FREE_BUFFER(varname_y);
		free(varname_y);
		PyErr_SetString(YDBPythonError, "failed to copy bytes object to buffer");
		return false;
	}

	dest->varname = varname_y;
	if (Py_None != subsarray) {
		sequence_len_ssize = PySequence_Length(subsarray);
		dest->subs_used = Py_SAFE_DOWNCAST(sequence_len_ssize, Py_ssize_t, unsigned int);
		subsarray_y = (ydb_buffer_t *)calloc(dest->subs_used, sizeof(ydb_buffer_t));
		convert_success = convert_py_sequence_to_ydb_buffer_array(subsarray, dest->subs_used, subsarray_y);
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
static void free_YDBKey(YDBKey *key) {
	YDB_FREE_BUFFER((key->varname));
	FREE_BUFFER_ARRAY(key->subsarray, key->subs_used);
}

/* Routine to validate a sequence of Python sequences representing keys. (Used
 * only by lock().)
 * Validation rule:
 *      1) key_sequence must be a sequence
 *      2) each item in key_sequence must be a sequence
 *      3) each item must be a sequence of 1 or 2 sub-items.
 *      4) item[0] must be a bytes object.
 *      5) item[1] either does not exist, is None or a sequence
 *      6) if item[1] is a sequence then it must contain only bytes objects.
 *
 * Parameters:
 *    keys_sequence        - a Python object that is to be validated.
 *    max_len              - the number of keys that are allowed in the keys_sequence
 *    error_message        - a preallocated string for storing the reason for validation failure.
 */
static int is_valid_key_sequence(PyObject *keys_sequence, int max_len) {
	Py_ssize_t i, len_keys, len_key_seq, len_varname;
	PyObject * key, *varname, *subsarray, *seq, *key_seq;
	bool	   error_encountered;

	/* validate key sequence type */
	seq = PySequence_Fast(keys_sequence, "'keys' argument must be a Sequence"); // New Reference
	if (!(PyTuple_Check(keys_sequence) || PyList_Check(keys_sequence))) {
		raise_ValidationError(YDBPython_TypeError, YDBPY_ERR_KEYS_INVALID, YDBPY_ERR_NOT_LIST_OR_TUPLE);
		DECREF_AND_RETURN(seq, FALSE);
	}

	/* validate key sequence length */
	len_keys = PySequence_Fast_GET_SIZE(seq);
	if (max_len < len_keys) {
		raise_ValidationError(YDBPython_ValueError, YDBPY_ERR_KEYS_INVALID, YDBPY_ERR_SEQUENCE_TOO_LONG, len_keys, max_len);
		DECREF_AND_RETURN(seq, FALSE);
	}

	/* validate each item/key in key sequence */
	error_encountered = FALSE;
	for (i = 0; i < len_keys; i++) {
		key = PySequence_Fast_GET_ITEM(seq, i); // Borrowed Reference
		key_seq = PySequence_Fast(key, "");	// New Reference
		len_key_seq = PySequence_Fast_GET_SIZE(key_seq);

		if (1 <= len_key_seq) {
			varname = PySequence_Fast_GET_ITEM(key_seq, 0); // Borrowed Reference
			if (PyUnicode_Check(varname)) {
				// Get length of Unicode object by multiplying code points by code point size
				len_varname = PyUnicode_GET_LENGTH(varname) * PyUnicode_KIND(varname);
			} else if (PyBytes_Check(varname)) {
				len_varname = PyBytes_Size(varname);
			} else {
				raise_ValidationError(YDBPython_TypeError, YDBPY_ERR_KEYS_INVALID,
						      YDBPY_ERR_VARNAME_NOT_BYTES_LIKE);
				Py_DECREF(key_seq);
				DECREF_AND_RETURN(seq, FALSE);
			}
		} else {
			varname = Py_None;
			len_varname = -1;
		}

		if (2 <= len_key_seq) {
			subsarray = PySequence_GetItem(key, 1); // Borrowed Reference
		} else {
			subsarray = Py_None;
		}

		if (!key_seq || ((2 == i) && !(PyTuple_Check(key) || PyList_Check(key)))) {
			/* Validate item/key type [list or tuple]. Only relevant for second
			 * item, i.e. subsarray, not varname
			 */
			raise_ValidationError(YDBPython_TypeError, YDBPY_ERR_KEYS_INVALID,
					      YDBPY_ERR_KEY_IN_SEQUENCE_NOT_LIST_OR_TUPLE, i);
			error_encountered = TRUE;
		} else if ((1 != len_key_seq) && (2 != len_key_seq)) {
			/* validate item/key length [1 or 2] */
			raise_ValidationError(YDBPython_ValueError, YDBPY_ERR_KEYS_INVALID,
					      YDBPY_ERR_KEY_IN_SEQUENCE_INCORRECT_LENGTH, i);
			error_encountered = TRUE;
		} else if ((!PyUnicode_Check(varname)) && (!PyBytes_Check(varname))) {
			/* validate item/key first element (varname) type */
			raise_ValidationError(YDBPython_TypeError, YDBPY_ERR_KEYS_INVALID, YDBPY_ERR_ITEM_NOT_BYTES_LIKE, i);
			error_encountered = TRUE;
		} else if (YDB_MAX_IDENT < len_varname) {
			/* validate item/key first element (varname) length */
			raise_ValidationError(YDBPython_ValueError, YDBPY_ERR_KEYS_INVALID,
					      YDBPY_ERR_KEY_IN_SEQUENCE_VARNAME_TOO_LONG, i, len_varname, YDB_MAX_IDENT);
			error_encountered = TRUE;
		} else if (2 == len_key_seq) {
			/* validate item/key second element (subsarray) if it exists */
			if (Py_None != subsarray) {
				int  copied;
				char tmp_prefix[YDBPY_MAX_ERRORMSG];
				char err_prefix[YDBPY_MAX_ERRORMSG];

				/* Incrementally build up error prefix format string to create
				 * a nested error message that contains full error information
				 * for the validation failure in the given sequence. This is
				 * needed to account for the fact that lock() accepts Sequences
				 * within Sequences. Specifically, it accepts a Sequence of
				 * YDBKeys, which are themselves Sequences. Accordingly, a full
				 * error message notes which YDBKey is invalid as well as which
				 * element of the YDBKey subsarray Sequence is invalid.
				 */
				copied = snprintf(tmp_prefix, YDBPY_MAX_ERRORMSG, YDBPY_ERR_KEYS_INVALID,
						  YDBPY_ERR_KEY_IN_SEQUENCE_SUBSARRAY_INVALID);
				assert(copied < YDBPY_MAX_ERRORMSG);
				UNUSED(copied);
				copied = snprintf(err_prefix, YDBPY_MAX_ERRORMSG, tmp_prefix, i, "%%s");
				assert(copied < YDBPY_MAX_ERRORMSG);
				UNUSED(copied);

				if (!is_valid_sequence(subsarray, YDBPython_KeySequence, err_prefix)) {
					error_encountered = TRUE;
				}
			}
		}

		Py_DECREF(key_seq);
		if (error_encountered) {
			DECREF_AND_RETURN(seq, FALSE);
		}
	}
	Py_DECREF(seq);
	return TRUE;
}

/* Takes an already validated (by 'validate_py_keys_sequence' above) PyObject sequence
 * that represents a series of keys loads that data into an already allocated array
 * of YDBKeys. (note: 'ret_keys' should later be freed by 'free_YDBKey_array' below)
 *
 * Parameters:
 *    sequence    - a Python object that has already been validated with 'validate_py_keys_sequence' or equivalent.
 */
static bool load_YDBKeys_from_key_sequence(PyObject *sequence, YDBKey *ret_keys) {
	bool	   success = true;
	Py_ssize_t i, len_keys;
	PyObject * key, *varname, *subsarray, *seq, *key_seq;

	seq = PySequence_Fast(sequence, "argument must be iterable"); // New Reference
	if (NULL != seq) {
		len_keys = PySequence_Fast_GET_SIZE(seq);
	} else {
		return false;
	}

	for (i = 0; i < len_keys; i++) {
		key = PySequence_Fast_GET_ITEM(seq, i);			     // Borrowed Reference
		key_seq = PySequence_Fast(key, "argument must be iterable"); // New Reference
		if (NULL == key_seq) {
			Py_DECREF(seq);
			return false;
		}

		varname = PySequence_Fast_GET_ITEM(key_seq, 0); // Borrowed Reference
		if (2 == PySequence_Fast_GET_SIZE(key_seq)) {
			subsarray = PySequence_Fast_GET_ITEM(key_seq, 1); // Borrowed Reference
		} else {
			subsarray = Py_None;
		}
		success = load_YDBKey(&ret_keys[i], varname, subsarray);
		Py_DECREF(key_seq);
		if (!success)
			break;
	}
	Py_DECREF(seq);
	return success;
}

/* Routine to free an array of YDBKeys as returned by above
 * 'load_YDBKeys_from_key_sequence'.
 *
 * Parameters:
 *    keysarray    - the array that is to be freed.
 *    len          - the number of elements in keysarray.
 */
static void free_YDBKey_array(YDBKey *keysarray, int len) {
	int i;
	if (NULL != keysarray) {
		for (i = 0; i < len; i++)
			if (NULL != &keysarray[i])
				free_YDBKey(&keysarray[i]);
		free(keysarray);
	}
}

/* Routine to help raise a YDBError. The caller still needs to return NULL for
 * the Exception to be raised.
 *
 * Parameters:
 *    status                 - the error code that is returned by the wrapped ydb_ function.
 */
static void raise_YDBError(int status) {
	ydb_char_t  error_string[YDBPY_MAX_ERRORMSG];
	PyObject *  message;
	int	    copied = 0, zstatus;
	char	    full_error_message[YDBPY_MAX_ERRORMSG];
	char *	    error_status, *api, *error_name, *error_message;
	char *	    next_field = NULL;
	const char *delim = ",";

	if (YDB_TP_ROLLBACK == status) {
		error_name = "%YDB-TP-ROLLBACK";
		error_message = " Transaction callback function returned YDB_TP_ROLLBACK.";
	} else if (YDB_TP_RESTART == status) {
		error_name = "%YDB-TP-RESTART";
		error_message = " Transaction callback function returned YDB_TP_RESTART.";
	} else if (YDB_LOCK_TIMEOUT == status) {
		error_name = "%YDB-LOCK-TIMEOUT";
		error_message = " Lock attempt timed out.";
	} else {
		zstatus = ydb_zstatus(error_string, YDBPY_MAX_ERRORMSG);
		if ((YDB_ERR_INVSTRLEN == zstatus) || (YDB_OK == zstatus)) {
			/* Normal error message format */
			error_name = strtok_r(error_string, delim, &next_field);
			error_message = strtok_r(NULL, delim, &next_field);
			if (NULL == error_message) {
				/* Alternate error message case */
				error_name = (NULL == error_status) ? error_status : "UNKNOWN";
				error_message = (NULL == api) ? api : "";
			}
		} else {
			assert(FALSE);
			error_name = "UNKNOWN";
			error_message = "";
		}
	}

	copied = snprintf(full_error_message, YDBPY_MAX_ERRORMSG, "%s (%d):%s", error_name, status, error_message);
	assert((0 <= copied) && (YDBPY_MAX_ERRORMSG > copied));
	UNUSED(copied);
	message = Py_BuildValue("s", full_error_message); // New Reference

	RAISE_SPECIFIC_ERROR(status, message);
	Py_DECREF(message);
}

/* API Wrappers */

/* FOR ALL BELOW WRAPPERS:
 * Each function converts Python types to the appropriate C types and passes them to the matching
 * YottaDB Simple API function then convert the return types and errors into appropriate Python types
 * and return those values
 *
 * Parameters:
 *    self        - the object that this method belongs to (in this case it's the _yottadb module.)
 *    args        - a Python tuple of the positional arguments passed to the function.
 *    kwds        - a Python dictionary of the keyword arguments passed to the function.
 */

/* Wrapper for ydb_data_s */
static PyObject *data(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	char *	      varname_py;
	int	      subs_used, status;
	Py_ssize_t    py_varname_len;
	unsigned int  ydb_varname_len, ret_value_ydb;
	PyObject *    subsarray_py, *ret_value_py;
	ydb_buffer_t  varname_ydb;
	ydb_buffer_t *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|O", kwlist, &varname_py, &py_varname_len, &subsarray_py))
		return NULL;

	/* validate */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "data()", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_data_s(&varname_ydb, subs_used, subsarray_ydb, &ret_value_ydb);
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}

		/* Create Python object to return */
		if (!return_NULL)
			ret_value_py = Py_BuildValue("I", ret_value_ydb); // New Reference
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);

	if (return_NULL)
		return NULL;
	else
		return ret_value_py;
}

/* Wrapper for ydb_delete_s() */
static PyObject *delete_wrapper(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	int	      deltype, status, subs_used;
	Py_ssize_t    py_varname_len;
	unsigned int  ydb_varname_len;
	char *	      varname_py;
	PyObject *    subsarray_py;
	ydb_buffer_t  varname_ydb;
	ydb_buffer_t *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;
	deltype = YDB_DEL_NODE;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", "delete_type", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|Oi", kwlist, &varname_py, &py_varname_len, &subsarray_py, &deltype)) {
		return NULL;
	}

	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "delete_wrapper()", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_delete_s(&varname_ydb, subs_used, subsarray_ydb, deltype);
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);

	if (return_NULL) {
		return NULL;
	} else {
		Py_INCREF(Py_None);
		return Py_None;
	}
}

/* Wrapper for ydb_delete_excl_s() */
static PyObject *delete_excel(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	bool	      success;
	int	      namecount, status;
	ydb_buffer_t *varnames_ydb;
	PyObject *    varnames_py;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	varnames_py = Py_None;

	/* parse and validate */
	static char *kwlist[] = {"varnames", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &varnames_py))
		return NULL;
	RETURN_IF_INVALID_SEQUENCE(varnames_py, YDBPython_VarnameSequence);

	/* Setup for Call */
	namecount = 0;
	if (Py_None != varnames_py)
		namecount = PySequence_Length(varnames_py);
	varnames_ydb = calloc(namecount, sizeof(ydb_buffer_t));
	if (0 < namecount) {
		success = convert_py_sequence_to_ydb_buffer_array(varnames_py, namecount, varnames_ydb);
		if (!success)
			return NULL;
	}

	status = ydb_delete_excl_s(namecount, varnames_ydb);
	/* check status for Errors and Raise Exception */
	if (YDB_OK != status) {
		raise_YDBError(status);
		return_NULL = true;
	}

	/* free allocated memory */
	FREE_BUFFER_ARRAY(varnames_ydb, namecount);

	if (return_NULL) {
		return NULL;
	} else {
		Py_INCREF(Py_None);
		return Py_None;
	}
}

/* Wrapper for ydb_get_s() */
static PyObject *get(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	int	      subs_used, status;
	unsigned int  ydb_varname_len;
	Py_ssize_t    py_varname_len;
	char *	      varname_py;
	PyObject *    subsarray_py, *ret_value_py;
	ydb_buffer_t  varname_ydb, ret_value_ydb;
	ydb_buffer_t *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|O", kwlist, &varname_py, &py_varname_len, &subsarray_py))
		return NULL;
	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "get()", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	YDB_MALLOC_BUFFER(&ret_value_ydb, YDBPY_DEFAULT_VALUE_LEN);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_get_s(&varname_ydb, subs_used, subsarray_ydb, &ret_value_ydb);

		/* Check to see if length of string was longer than YDBPY_DEFAULT_VALUE_LEN. If so, try again
		 * with proper length */
		if (YDB_ERR_INVSTRLEN == status) {
			FIX_BUFFER_LENGTH(ret_value_ydb);
			/* Call the wrapped function */
			status = ydb_get_s(&varname_ydb, subs_used, subsarray_ydb, &ret_value_ydb);
			assert(YDB_ERR_INVSTRLEN != status);
		}
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}
		/* Create Python object to return */
		if (!return_NULL) {
			/* New Reference */
			ret_value_py = Py_BuildValue("y#", ret_value_ydb.buf_addr, (Py_ssize_t)ret_value_ydb.len_used);
		}
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);
	YDB_FREE_BUFFER(&ret_value_ydb);

	if (return_NULL)
		return NULL;
	else
		return ret_value_py;
}

/* Wrapper for ydb_incr_s() */
static PyObject *incr(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	int	      status, subs_used;
	Py_ssize_t    py_varname_len, increment_py_len;
	unsigned int  ydb_varname_len, increment_ydb_len;
	char *	      varname_py, *increment_py;
	PyObject *    subsarray_py, *ret_value_py;
	ydb_buffer_t  increment_ydb, ret_value_ydb, varname_ydb;
	ydb_buffer_t *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;
	increment_py = "1";
	increment_py_len = 1;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", "increment", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|Os#", kwlist, &varname_py, &py_varname_len, &subsarray_py, &increment_py,
					 &increment_py_len)) {
		return NULL;
	}

	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);
	INVOKE_PY_SAFE_DOWNCAST(increment_ydb_len, increment_py_len, FALSE);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "incr() for varname", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	POPULATE_NEW_BUFFER(increment_py, increment_ydb, increment_ydb_len, "incr() for increment", return_NULL);
	YDB_MALLOC_BUFFER(&ret_value_ydb, MAX_CONONICAL_NUMBER_STRING_MAX);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_incr_s(&varname_ydb, subs_used, subsarray_ydb, &increment_ydb, &ret_value_ydb);
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}

		/* Create Python object to return */
		if (!return_NULL) {
			/* New Reference */
			ret_value_py = Py_BuildValue("y#", ret_value_ydb.buf_addr, (Py_ssize_t)ret_value_ydb.len_used);
		}
	}
	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);
	YDB_FREE_BUFFER(&increment_ydb);
	YDB_FREE_BUFFER(&ret_value_ydb);

	if (return_NULL) {
		return NULL;
	} else {
		return ret_value_py;
	}
}

/* Wrapper for ydb_lock_s() */
static PyObject *lock(PyObject *self, PyObject *args, PyObject *kwds) {
	bool		   return_NULL = false;
	bool		   success = true;
	int		   len_keys, status;
	unsigned long long timeout_nsec;
	PyObject *	   keys_py;
	YDBKey *	   keys_ydb = NULL;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	timeout_nsec = 0;
	keys_py = Py_None;

	/* parse and validate */
	static char *kwlist[] = {"keys", "timeout_nsec", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OK", kwlist, &keys_py, &timeout_nsec))
		return NULL;
	if (Py_None != keys_py) {
		if (!is_valid_key_sequence(keys_py, YDB_LOCK_MAX_KEYS)) {
			return NULL;
		}
	}
	if (Py_None == keys_py) {
		len_keys = 0;
	} else {
		len_keys = Py_SAFE_DOWNCAST(PySequence_Length(keys_py), Py_ssize_t, int);
	}

	/* Setup for Call */
	if (Py_None != keys_py) {
		keys_ydb = (YDBKey *)calloc(len_keys, sizeof(YDBKey));
		success = load_YDBKeys_from_key_sequence(keys_py, keys_ydb);
		if (!success) {
			return NULL;
		}
	}

	if (!return_NULL) {
		void **arg_values;
		int    num_args, cur_key, cur_index;

		num_args = YDB_LOCK_MIN_ARGS + (len_keys * YDB_LOCK_ARGS_PER_KEY);
		arg_values = (void **)malloc(sizeof(void *) * (num_args + 1)); // +1 for num_args variable itself

		arg_values[0] = (void *)(uintptr_t)num_args;
		arg_values[1] = (void *)timeout_nsec;
		arg_values[2] = (void *)(uintptr_t)len_keys;

		/* Initialize arg_values index to the first location after the elements
		 * initialized above.
		 */
		cur_index = YDB_LOCK_MIN_ARGS + 1; // +1 for num_args variable itself
		for (cur_key = 0; cur_key < len_keys; cur_key++) {
			arg_values[cur_index] = (void *)keys_ydb[cur_key].varname;
			arg_values[cur_index + 1] = (void *)(uintptr_t)keys_ydb[cur_key].subs_used;
			arg_values[cur_index + 2] = (void *)keys_ydb[cur_key].subsarray;
			cur_index += YDB_LOCK_ARGS_PER_KEY;
		}

		status = ydb_call_variadic_plist_func((ydb_vplist_func)&ydb_lock_s, (uintptr_t)arg_values);
		free(arg_values);
		/* check for errors */
		if (YDB_LOCK_TIMEOUT == status) {
			PyErr_SetString(YDBTimeoutError, "Not able to acquire all requested locks in the specified time.");
			return_NULL = true;
		} else if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}
	}

	/* free allocated memory */
	free_YDBKey_array(keys_ydb, len_keys);

	if (return_NULL) {
		return NULL;
	} else {
		Py_INCREF(Py_None);
		return Py_None;
	}
}

/* Wrapper for ydb_lock_decr_s() */
static PyObject *lock_decr(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	int	      status, subs_used;
	Py_ssize_t    py_varname_len;
	unsigned int  ydb_varname_len;
	char *	      varname_py;
	PyObject *    subsarray_py;
	ydb_buffer_t  varname_ydb;
	ydb_buffer_t *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|O", kwlist, &varname_py, &py_varname_len, &subsarray_py))
		return NULL;
	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "lock_decr()", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_lock_decr_s(&varname_ydb, subs_used, subsarray_ydb);
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);

	if (return_NULL) {
		return NULL;
	} else {
		Py_INCREF(Py_None);
		return Py_None;
	}
}

/* Wrapper for ydb_lock_incr_s() */
static PyObject *lock_incr(PyObject *self, PyObject *args, PyObject *kwds) {
	bool		   return_NULL = false;
	int		   status, subs_used;
	Py_ssize_t	   py_varname_len;
	unsigned int	   ydb_varname_len;
	char *		   varname_py;
	unsigned long long timeout_nsec;
	PyObject *	   subsarray_py;
	ydb_buffer_t	   varname_ydb;
	ydb_buffer_t *	   subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;
	timeout_nsec = 0;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", "timeout_nsec", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|OL", kwlist, &varname_py, &py_varname_len, &subsarray_py, &timeout_nsec)) {
		return NULL;
	}
	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "lock_incr()", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_lock_incr_s(timeout_nsec, &varname_ydb, subs_used, subsarray_ydb);
		/* check status for Errors and Raise Exception */
		if (YDB_LOCK_TIMEOUT == status) {
			PyErr_SetString(YDBTimeoutError, "Not able to acquire all requested locks in the specified time.");
			return_NULL = true;
		} else if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);

	if (return_NULL) {
		return NULL;
	} else {
		Py_INCREF(Py_None);
		return Py_None;
	}
}

/* Wrapper for ydb_node_next_s() */
static PyObject *node_next(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	int	      max_subscript_string, ret_subsarray_num_elements, ret_subs_used, status, subs_used;
	Py_ssize_t    py_varname_len;
	unsigned int  ydb_varname_len;
	char *	      varname_py;
	PyObject *    subsarray_py, *ret_subsarray_py;
	ydb_buffer_t  varname_ydb;
	ydb_buffer_t *ret_subsarray_ydb, *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|O", kwlist, &varname_py, &py_varname_len, &subsarray_py))
		return NULL;

	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "node_next()", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	max_subscript_string = YDBPY_DEFAULT_SUBSCRIPT_LEN;
	ret_subsarray_num_elements = YDBPY_DEFAULT_SUBSCRIPT_COUNT;
	ret_subs_used = ret_subsarray_num_elements;
	ret_subsarray_ydb = create_empty_buffer_array(ret_subs_used, max_subscript_string);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_node_next_s(&varname_ydb, subs_used, subsarray_ydb, &ret_subs_used, ret_subsarray_ydb);

		/* If not enough buffers in ret_subsarray */
		if (YDB_ERR_INSUFFSUBS == status) {
			FREE_BUFFER_ARRAY(ret_subsarray_ydb, ret_subsarray_num_elements);
			ret_subsarray_num_elements = ret_subs_used;
			ret_subsarray_ydb = create_empty_buffer_array(ret_subs_used, max_subscript_string);
			/* recall the wrapped function */
			status = ydb_node_next_s(&varname_ydb, subs_used, subsarray_ydb, &ret_subs_used, ret_subsarray_ydb);
		}

		/* if a buffer is not long enough */
		while (YDB_ERR_INVSTRLEN == status) {
			FIX_BUFFER_LENGTH(ret_subsarray_ydb[ret_subs_used]);
			ret_subs_used = ret_subsarray_num_elements;
			/* recall the wrapped function */
			status = ydb_node_next_s(&varname_ydb, subs_used, subsarray_ydb, &ret_subs_used, ret_subsarray_ydb);
		}
		assert(YDB_ERR_INVSTRLEN != status);
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}
		/* Create Python object to return */
		if (!return_NULL) {
			/* New Reference */
			ret_subsarray_py = convert_ydb_buffer_array_to_py_tuple(ret_subsarray_ydb, ret_subs_used);
		}
	}
	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);
	FREE_BUFFER_ARRAY(ret_subsarray_ydb, ret_subsarray_num_elements);

	if (return_NULL)
		return NULL;
	else
		return ret_subsarray_py;
}

/* Wrapper for ydb_node_previous_s() */
static PyObject *node_previous(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	int	      max_subscript_string, ret_subsarray_num_elements, ret_subs_used, status, subs_used;
	Py_ssize_t    py_varname_len;
	unsigned int  ydb_varname_len;
	char *	      varname_py;
	PyObject *    subsarray_py, *ret_subsarray_py;
	ydb_buffer_t  varname_ydb;
	ydb_buffer_t *ret_subsarray_ydb, *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|O", kwlist, &varname_py, &py_varname_len, &subsarray_py))
		return NULL;

	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "node_previous()", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);

	max_subscript_string = YDBPY_DEFAULT_SUBSCRIPT_LEN;
	ret_subsarray_num_elements = YDBPY_DEFAULT_SUBSCRIPT_COUNT;
	ret_subs_used = ret_subsarray_num_elements;
	ret_subsarray_ydb = create_empty_buffer_array(ret_subs_used, max_subscript_string);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_node_previous_s(&varname_ydb, subs_used, subsarray_ydb, &ret_subs_used, ret_subsarray_ydb);

		/* if a buffer is not long enough */
		while (YDB_ERR_INVSTRLEN == status) {
			FIX_BUFFER_LENGTH(ret_subsarray_ydb[ret_subs_used]);
			ret_subs_used = ret_subsarray_num_elements;
			/* recall the wrapped function */
			status = ydb_node_previous_s(&varname_ydb, subs_used, subsarray_ydb, &ret_subs_used, ret_subsarray_ydb);
		}
		assert(YDB_ERR_INVSTRLEN != status);
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}

		/* Create Python object to return */
		if (!return_NULL) {
			/* New Reference */
			ret_subsarray_py = convert_ydb_buffer_array_to_py_tuple(ret_subsarray_ydb, ret_subs_used);
		}
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);
	FREE_BUFFER_ARRAY(ret_subsarray_ydb, ret_subsarray_num_elements);

	if (return_NULL)
		return NULL;
	else
		return ret_subsarray_py;
}

/* Wrapper for ydb_set_s() */
static PyObject *set(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	int	      status = YDB_OK, subs_used;
	Py_ssize_t    py_varname_len, value_py_len;
	unsigned int  ydb_varname_len, value_ydb_len;
	char *	      varname_py, *value_py;
	PyObject *    subsarray_py;
	ydb_buffer_t  value_ydb, varname_ydb;
	ydb_buffer_t *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;
	value_py = "";
	value_py_len = 0;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", "value", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|Os#", kwlist, &varname_py, &py_varname_len, &subsarray_py, &value_py,
					 &value_py_len)) {
		return NULL;
	}
	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);
	INVOKE_PY_SAFE_DOWNCAST(value_ydb_len, value_py_len, FALSE);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "set() for varname", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	POPULATE_NEW_BUFFER(value_py, value_ydb, value_ydb_len, "set() for value", return_NULL);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_set_s(&varname_ydb, subs_used, subsarray_ydb, &value_ydb);
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	YDB_FREE_BUFFER(&value_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);

	if (return_NULL)
		return NULL;
	else {
		Py_INCREF(Py_None);
		return Py_None;
	}
}

/* Wrapper for ydb_str2zwr_s() */
static PyObject *str2zwr(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	     return_NULL = false;
	int	     status;
	Py_ssize_t   str_py_len;
	unsigned int str_ydb_len;
	char *	     str_py;
	ydb_buffer_t str_ydb, zwr_ydb;
	PyObject *   zwr_py;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	str_py = "";
	str_py_len = 0;

	/* parse and validate */
	static char *kwlist[] = {"input", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|", kwlist, &str_py, &str_py_len))
		return NULL;
	INVOKE_PY_SAFE_DOWNCAST(str_ydb_len, str_py_len, FALSE);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(str_py, str_ydb, str_ydb_len, "ydb_str2zwr", return_NULL);
	YDB_MALLOC_BUFFER(&zwr_ydb, YDBPY_DEFAULT_VALUE_LEN);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_str2zwr_s(&str_ydb, &zwr_ydb);

		/* recall with properly sized buffer if zwr_buf is not long enough */
		if (YDB_ERR_INVSTRLEN == status) {
			FIX_BUFFER_LENGTH(zwr_ydb);
			/* recall the wrapped function */
			status = ydb_str2zwr_s(&str_ydb, &zwr_ydb);
			assert(YDB_ERR_INVSTRLEN != status);
		}
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}

		/* Create Python object to return */
		if (!return_NULL) {
			/* New Reference */
			zwr_py = Py_BuildValue("y#", zwr_ydb.buf_addr, (Py_ssize_t)zwr_ydb.len_used);
		}
	}
	/* free allocated memory */
	YDB_FREE_BUFFER(&str_ydb);
	YDB_FREE_BUFFER(&zwr_ydb);

	if (return_NULL)
		return NULL;
	else
		return zwr_py;
}

/* Wrapper for ydb_subscript_next_s() */
static PyObject *subscript_next(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	int	      status, subs_used;
	Py_ssize_t    py_varname_len;
	unsigned int  ydb_varname_len;
	char *	      varname_py;
	PyObject *    subsarray_py, *ret_value_py;
	ydb_buffer_t  ret_value_ydb, varname_ydb;
	ydb_buffer_t *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;

	/* parse and validate */
	static char *kwlist[] = {"varname", "subsarray", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|O", kwlist, &varname_py, &py_varname_len, &subsarray_py))
		return NULL;

	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "subscript_next()", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	YDB_MALLOC_BUFFER(&ret_value_ydb, YDBPY_DEFAULT_SUBSCRIPT_LEN);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_subscript_next_s(&varname_ydb, subs_used, subsarray_ydb, &ret_value_ydb);

		/* check to see if length of string was longer than YDBPY_DEFAULT_SUBSCRIPT_LEN is so, try again
		 * with proper length */
		if (YDB_ERR_INVSTRLEN == status) {
			FIX_BUFFER_LENGTH(ret_value_ydb);
			/* recall the wrapped function */
			status = ydb_subscript_next_s(&varname_ydb, subs_used, subsarray_ydb, &ret_value_ydb);
			assert(YDB_ERR_INVSTRLEN != status);
		}
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}

		/* Create Python object to return */
		if (!return_NULL) {
			/* New Reference */
			ret_value_py = Py_BuildValue("s#", ret_value_ydb.buf_addr, (Py_ssize_t)ret_value_ydb.len_used);
		}
	}
	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);
	YDB_FREE_BUFFER(&ret_value_ydb);

	if (return_NULL)
		return NULL;
	else
		return ret_value_py;
}

/* Wrapper for ydb_subscript_previous_s() */
static PyObject *subscript_previous(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	int	      status, subs_used;
	Py_ssize_t    py_varname_len;
	unsigned int  ydb_varname_len;
	char *	      varname_py;
	PyObject *    subsarray_py, *ret_value_py;
	ydb_buffer_t  ret_value_ydb, varname_ydb;
	ydb_buffer_t *subsarray_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	subsarray_py = Py_None;

	/* Setup for Call */
	static char *kwlist[] = {"varname", "subsarray", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|O", kwlist, &varname_py, &py_varname_len, &subsarray_py))
		return NULL;

	/* validate varname */
	INVOKE_PY_SAFE_DOWNCAST(ydb_varname_len, py_varname_len, TRUE);
	RETURN_IF_INVALID_SEQUENCE(subsarray_py, YDBPython_SubsarraySequence);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(varname_py, varname_ydb, ydb_varname_len, "subscript_previous()", return_NULL);
	POPULATE_SUBS_USED_AND_SUBSARRAY(subsarray_py, subs_used, subsarray_ydb, return_NULL);
	YDB_MALLOC_BUFFER(&ret_value_ydb, YDBPY_DEFAULT_SUBSCRIPT_LEN);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_subscript_previous_s(&varname_ydb, subs_used, subsarray_ydb, &ret_value_ydb);

		/* check to see if length of string was longer than YDBPY_DEFAULT_SUBSCRIPT_LEN is so, try again
		 * with proper length */
		if (YDB_ERR_INVSTRLEN == status) {
			FIX_BUFFER_LENGTH(ret_value_ydb);
			status = ydb_subscript_previous_s(&varname_ydb, subs_used, subsarray_ydb, &ret_value_ydb);
			assert(YDB_ERR_INVSTRLEN != status);
		}
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}

		/* Create Python object to return */
		if (!return_NULL) {
			/* New Reference */
			ret_value_py = Py_BuildValue("s#", ret_value_ydb.buf_addr, (Py_ssize_t)ret_value_ydb.len_used);
		}
	}
	/* free allocated memory */
	YDB_FREE_BUFFER(&varname_ydb);
	FREE_BUFFER_ARRAY(subsarray_ydb, subs_used);
	YDB_FREE_BUFFER(&ret_value_ydb);

	if (return_NULL)
		return NULL;
	else
		return ret_value_py;
}

/* Callback functions used by Wrapper for ydb_tp_s() */

/* Callback Wrapper used by tp_st. The approach of calling a Python function is a
 * bit of a hack. Here's how it works:
 *      1) This is the callback function that is always passed to the ydb_tp_st
 *              Simple API function and should only ever be called by ydb_tp_st
 *              via tp() below. It assumes that everything passed to it was validated.
 *      2) The actual Python function to be called is passed to this function
 *              as the first element in a Python tuple.
 *      3) The positional arguments are passed as the second element and the
 *              keyword args are passed as the third.
 *      4) This function calls calls the Python callback function with the args and
 *              kwargs arguments.
 *      5) if a function raises an exception then this function returns TPCALLBACKINVRETVAL
 *              as a way of indicating an error.
 *      Note: the PyErr String is already set so the the function receiving the return
 *              value (tp()) just needs to return NULL.
 */
static int callback_wrapper(void *function_with_arguments) {
	int	  ret_value_ydb;
	bool	  decref_args = false;
	bool	  decref_kwargs = false;
	PyObject *function, *args, *kwargs, *ret_value_py;

	function = PyTuple_GetItem(function_with_arguments, 0); // Borrowed Reference
	args = PyTuple_GetItem(function_with_arguments, 1);	// Borrowed Reference
	kwargs = PyTuple_GetItem(function_with_arguments, 2);	// Borrowed Reference

	if (Py_None == args) {
		args = PyTuple_New(0);
		decref_args = true;
	}
	if (Py_None == kwargs) {
		kwargs = PyDict_New();
		decref_kwargs = true;
	}

	ret_value_py = PyObject_Call(function, args, kwargs); // New Reference

	if (decref_args)
		Py_DECREF(args);
	if (decref_kwargs)
		Py_DECREF(kwargs);

	if (NULL == ret_value_py) {
		/* `function` raised an exception.
		 *      Note: Do not need to `PyErr_SetString` because this or similar operation
		 *              was done when the exception was raised by `function`
		 */
		return YDB_ERR_TPCALLBACKINVRETVAL;
	} else if (!PyLong_Check(ret_value_py)) {
		PyErr_SetString(PyExc_TypeError, "Callback function must return value of type int.");
		return YDB_ERR_TPCALLBACKINVRETVAL;
	}
	ret_value_ydb = (int)PyLong_AsLong(ret_value_py);
	Py_DECREF(ret_value_py);

	return ret_value_ydb;
}

/* Wrapper for ydb_tp_s() */
static PyObject *tp(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	      return_NULL = false;
	bool	      success;
	int	      namecount, status;
	char *	      transid;
	PyObject *    callback, *callback_args, *callback_kwargs, *varnames_py, *function_with_arguments;
	ydb_buffer_t *varnames_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	callback_args = Py_None;
	callback_kwargs = Py_None;
	transid = "BATCH";
	namecount = 0;
	varnames_py = Py_None;

	/* parse and validate */
	static char *kwlist[] = {"callback", "args", "kwargs", "transid", "varnames", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOsO", kwlist, &callback, &callback_args, &callback_kwargs, &transid,
					 &varnames_py)) {
		return_NULL = true;
	}

	/* validate input */
	if (!PyCallable_Check(callback)) {
		PyErr_SetString(PyExc_TypeError, "'callback' must be a callable.");
		return_NULL = true;
	}
	if ((Py_None != callback_args) && !PyTuple_Check(callback_args)) {
		PyErr_SetString(PyExc_TypeError, "'args' must be a tuple. "
						 "(It will be passed to the callback "
						 "function as positional arguments.)");
		return_NULL = true;
	}
	if ((Py_None != callback_kwargs) && !PyDict_Check(callback_kwargs)) {
		PyErr_SetString(PyExc_TypeError, "'kwargs' must be a dictionary. "
						 "(It will be passed to the callback function as keyword arguments.)");
		return_NULL = true;
	}
	RETURN_IF_INVALID_SEQUENCE(varnames_py, YDBPython_VarnameSequence);
	if (!return_NULL) {
		/* Setup for Call */
		/* New Reference */
		function_with_arguments = Py_BuildValue("(OOO)", callback, callback_args, callback_kwargs);
		namecount = 0;
		if (Py_None != varnames_py)
			namecount = PySequence_Length(varnames_py);

		varnames_ydb = (ydb_buffer_t *)calloc(namecount, sizeof(ydb_buffer_t));
		if (0 < namecount) {
			success = convert_py_sequence_to_ydb_buffer_array(varnames_py, namecount, varnames_ydb);
			if (!success)
				return NULL;
		}

		/* Call the wrapped function */
		status = ydb_tp_s(callback_wrapper, function_with_arguments, transid, namecount, varnames_ydb);
		/* check status for Errors and Raise Exception */
		if (YDB_ERR_TPCALLBACKINVRETVAL == status) {
			// Exception already raised in callback_wrapper
			return_NULL = true;
		} else if (YDB_TP_RESTART == status) {
			PyErr_SetString(YDBTPRestart, "tp() callback function returned 'YDB_TP_RESTART'.");
			return_NULL = true;
		} else if (YDB_TP_ROLLBACK == status) {
			PyErr_SetString(YDBTPRollback, "tp() callback function returned 'YDB_TP_ROLLBACK'.");
			return_NULL = true;
		} else if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}
		/* free allocated memory */
		Py_DECREF(function_with_arguments);
		free(varnames_ydb);
	}

	if (return_NULL) {
		return NULL;
	} else {
		return Py_BuildValue("i", status);
	}
}

/* Wrapper for ydb_zwr2str_s() */
static PyObject *zwr2str(PyObject *self, PyObject *args, PyObject *kwds) {
	bool	     return_NULL = false;
	int	     status;
	Py_ssize_t   zwr_py_len;
	unsigned int zwr_ydb_len;
	char *	     zwr_py;
	PyObject *   str_py;
	ydb_buffer_t zwr_ydb, str_ydb;

	UNUSED(self);
	/* Default values for optional arguments passed from Python */
	zwr_py = "";
	zwr_py_len = 0;

	/* parse and validate */
	static char *kwlist[] = {"input", NULL};
	/* Parsed values are borrowed references, do not Py_DECREF them. */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#|K", kwlist, &zwr_py, &zwr_py_len))
		return NULL;
	INVOKE_PY_SAFE_DOWNCAST(zwr_ydb_len, zwr_py_len, FALSE);

	/* Setup for Call */
	POPULATE_NEW_BUFFER(zwr_py, zwr_ydb, zwr_ydb_len, "zwr2str()", return_NULL);
	YDB_MALLOC_BUFFER(&str_ydb, YDBPY_DEFAULT_VALUE_LEN);
	if (!return_NULL) {
		/* Call the wrapped function */
		status = ydb_zwr2str_s(&zwr_ydb, &str_ydb);
		/* recall with properly sized buffer if zwr_ydb is not long enough */
		if (YDB_ERR_INVSTRLEN == status) {
			FIX_BUFFER_LENGTH(str_ydb);
			/* recall the wrapped function */
			status = ydb_zwr2str_s(&zwr_ydb, &str_ydb);
			assert(YDB_ERR_INVSTRLEN != status);
		}
		/* check status for Errors and Raise Exception */
		if (YDB_OK != status) {
			raise_YDBError(status);
			return_NULL = true;
		}
		if (!return_NULL) {
			/* New Reference */
			str_py = Py_BuildValue("y#", str_ydb.buf_addr, (Py_ssize_t)str_ydb.len_used);
		}
	}
	YDB_FREE_BUFFER(&zwr_ydb);
	YDB_FREE_BUFFER(&str_ydb);

	if (return_NULL)
		return NULL;
	else
		return str_py;
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
/* First we will create an array of structs that represent the methods in the module.
 * (https://docs.python.org/3/c-api/structures.html#c.PyMethodDef)
 * Each struct has 4 elements:
 *      1. The Python name of the method
 *      2. The C function that is to be called when the method is called
 *      3. How the function can be called (all these functions allow for calling with both positional and keyword arguments)
 *      4. The docstring for this method (to be used by help() in the Python REPL)
 * The final struct is a sentinel value to indicate the end of the array (i.e. {NULL, NULL, 0, NULL})
 */
static PyMethodDef methods[] = {
    /* Simple and Simple Threaded API Functions */
    {"data", (PyCFunction)data, METH_VARARGS | METH_KEYWORDS,
     "used to learn what type of data is at a node.\n "
     "0 : There is neither a value nor a subtree, "
     "i.e., it is undefined.\n"
     "1 : There is a value, but no subtree\n"
     "10 : There is no value, but there is a subtree.\n"
     "11 : There are both a value and a subtree.\n"},
    {"delete", (PyCFunction)delete_wrapper, METH_VARARGS | METH_KEYWORDS, "deletes node value or tree data at node"},
    {"delete_excel", (PyCFunction)delete_excel, METH_VARARGS | METH_KEYWORDS,
     "delete the trees of all local variables "
     "except those in the 'varnames' array"},
    {"get", (PyCFunction)get, METH_VARARGS | METH_KEYWORDS, "returns the value of a node or raises exception"},
    {"incr", (PyCFunction)incr, METH_VARARGS | METH_KEYWORDS, "increments value by the value specified by 'increment'"},

    {"lock", (PyCFunction)lock, METH_VARARGS | METH_KEYWORDS, "..."},

    {"lock_decr", (PyCFunction)lock_decr, METH_VARARGS | METH_KEYWORDS,
     "Decrements the count of the specified lock held "
     "by the process. As noted in the Concepts section, a "
     "lock whose count goes from 1 to 0 is released. A lock "
     "whose name is specified, but which the process does "
     "not hold, is ignored."},
    {"lock_incr", (PyCFunction)lock_incr, METH_VARARGS | METH_KEYWORDS,
     "Without releasing any locks held by the process, "
     "attempt to acquire the requested lock incrementing it"
     " if already held."},
    {"node_next", (PyCFunction)node_next, METH_VARARGS | METH_KEYWORDS,
     "facilitate depth-first traversal of a local or global"
     " variable tree. returns string tuple of subscripts of"
     " next node with value."},
    {"node_previous", (PyCFunction)node_previous, METH_VARARGS | METH_KEYWORDS,
     "facilitate depth-first traversal of a local "
     "or global variable tree. returns string tuple"
     "of subscripts of previous node with value."},
    {"set", (PyCFunction)set, METH_VARARGS | METH_KEYWORDS, "sets the value of a node or raises exception"},
    {"str2zwr", (PyCFunction)str2zwr, METH_VARARGS | METH_KEYWORDS,
     "returns the zwrite formatted (Bytes Object) version of the"
     " Bytes object provided as input."},
    {"subscript_next", (PyCFunction)subscript_next, METH_VARARGS | METH_KEYWORDS,
     "returns the name of the next subscript at "
     "the same level as the one given"},
    {"subscript_previous", (PyCFunction)subscript_previous, METH_VARARGS | METH_KEYWORDS,
     "returns the name of the previous "
     "subscript at the same level as the "
     "one given"},
    {"tp", (PyCFunction)tp, METH_VARARGS | METH_KEYWORDS, "transaction"},

    {"zwr2str", (PyCFunction)zwr2str, METH_VARARGS | METH_KEYWORDS,
     "returns the Bytes Object from the zwrite formated Bytes "
     "object provided as input."},
    /* API Utility Functions */
    {NULL, NULL, 0, NULL} /* Sentinel */
};

/* The _yottadbmodule struct contains the information about the Python module per:
 * https://docs.python.org/3/c-api/module.html#initializing-c-modules
 *
 * A number of PyModuleDef struct members are unused by YDBPython, and so are
 * set to NULL to prevent compiler warnings.
 *
 * For a full list of PyModuleDef struct members, see:
 * https://docs.python.org/3/c-api/module.html#c.PyModuleDef
 */
static struct PyModuleDef _yottadbmodule = {PyModuleDef_HEAD_INIT,
					    "_yottadb",
					    "A module that provides basic access to the YottaDB's Simple API",
					    -1,
					    methods,
					    NULL,
					    NULL,
					    NULL,
					    NULL};

/* The initialization function for _yottadb.
 * This function must be named PyInit_{name of Module}
 */
PyMODINIT_FUNC PyInit__yottadb(void) {
	/* Initialize the module */
	PyObject *module = PyModule_Create(&_yottadbmodule);

	/* Defining Module 'Constants' */
	PyObject *module_dictionary = PyModule_GetDict(module);

	/* Expose constants defined in libyottadb.h */
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

	/* expose useful constants from libydberrors.h or libydberrors2.h */
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPTIMEOUT", Py_BuildValue("i", YDB_ERR_TPTIMEOUT));

	/* expose useful constants defined in _yottadb.h */
	PyDict_SetItemString(module_dictionary, "YDB_LOCK_MAX_KEYS", Py_BuildValue("i", YDB_LOCK_MAX_KEYS));

	/* Adding Exceptions */
	/* Step 1: create exception with PyErr_NewException.
		Arguments: (https://docs.python.org/3/c-api/exceptions.html#c.PyErr_NewException)
	 *              1. Name of the exception in 'module.classname' form
	 *              2. The Base class of the exception (NULL if base class is Pythons base Exception class
	 *              3. A dictionary of class variables and methods. (Usually NULL)
	 * Step 2: Add this Exception to the module using PyModule_AddObject
		(https://docs.python.org/3/c-api/module.html#c.PyModule_AddObject)
	 */
	YDBException = PyErr_NewException("_yottadb.YDBException", NULL, NULL);
	PyModule_AddObject(module, "YDBException", YDBException);

	YDBTPException = PyErr_NewException("_yottadb.YDBTPException", YDBException, NULL);
	PyModule_AddObject(module, "YDBTPException", YDBTPException);

	YDBTPRollback = PyErr_NewException("_yottadb.YDBTPRollback", YDBTPException, NULL);
	PyModule_AddObject(module, "YDBTPRollback", YDBTPRollback);

	YDBTPRestart = PyErr_NewException("_yottadb.YDBTPRestart", YDBTPException, NULL);
	PyModule_AddObject(module, "YDBTPRestart", YDBTPRestart);

	YDBTimeoutError = PyErr_NewException("_yottadb.YDBTimeoutError", YDBException, NULL);
	PyModule_AddObject(module, "YDBTimeoutError", YDBTimeoutError);

	YDBPythonError = PyErr_NewException("_yottadb.YDBPythonError", YDBException, NULL);
	PyModule_AddObject(module, "YDBPythonError", YDBPythonError);

	YDBError = PyErr_NewException("_yottadb.YDBError", YDBException, NULL);
	PyModule_AddObject(module, "YDBError", YDBError);
	/* add auto generated Exceptions from libydberrors.h or libydberrors2.h */
	ADD_YDBERRORS();

	/* return the now fully initialized module */
	return module;
}
