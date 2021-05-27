/****************************************************************
 *                                                              *
 * Copyright (c) 2020-2021 Peter Goss All rights reserved.      *
 *                                                              *
 * Copyright (c) 2020-2021 YottaDB LLC and/or its subsidiaries. *
 * All rights reserved.                                         *
 *                                                              *
 *  This source code contains the intellectual property         *
 *  of its copyright holder(s), and is made available           *
 *  under a license.  If you do not know the terms of           *
 *  the license, please stop and do not read further.           *
 *                                                              *
 ****************************************************************/

/* A structure that represents a key using ydb c types. used internally for
 * converting between python and ydb c types */

#define YDBPY_DEFAULT_VALUE_LEN		32
#define YDBPY_DEFAULT_SUBSCRIPT_LEN	16
#define YDBPY_DEFAULT_SUBSCRIPT_COUNT	2
#define MAX_CONONICAL_NUMBER_STRING_MAX 48

#define YDB_LOCK_MIN_ARGS		2
#define YDB_LOCK_ARGS_PER_KEY		3
#define YDB_CALL_VARIADIC_MAX_ARGUMENTS 36
#define YDB_LOCK_MAX_KEYS		(YDB_CALL_VARIADIC_MAX_ARGUMENTS - YDB_LOCK_MIN_ARGS) / YDB_LOCK_ARGS_PER_KEY

#define YDBPY_MAX_ERRORMSG 2048

/* Set of acceptable Python error types. Each type is named by prefixing a Python error name with `YDBPython`,
 * with the exception of YDBPython_NoError. This item doesn't represent a Python error, but is included at enum value 0
 * to prevent conflicts with YDB_OK which signals no error with a value of 0.
 */
typedef enum YDBPythonErrorType {
	YDBPython_TypeError = 1,
	YDBPython_ValueError,
} YDBPythonErrorType;

/* Set of acceptable Python Sequence types. Used to enforce correct limits and
 * emit relevant errors when processing Python Sequences passed down from
 * yottadb.py.
 */
typedef enum YDBPythonSequenceType {
	YDBPython_VarnameSequence,
	YDBPython_SubsarraySequence,
	YDBPython_KeySequence,
} YDBPythonSequenceType;

// TypeError messages
#define YDBPY_ERR_NOT_LIST_OR_TUPLE		    "key must be list or tuple."
#define YDBPY_ERR_VARNAME_NOT_BYTES_LIKE	    "varname argument is not a bytes-like object (bytes or str)"
#define YDBPY_ERR_ITEM_NOT_BYTES_LIKE		    "item %ld is not a bytes-like object (bytes or str)"
#define YDBPY_ERR_KEY_IN_SEQUENCE_NOT_LIST_OR_TUPLE "item %ld is not a list or tuple."
#define YDBPY_ERR_KEY_IN_SEQUENCE_VARNAME_NOT_BYTES "item %ld in key sequence invalid: first element must be of type 'bytes'"

// ValueError messages
#define YDBPY_ERR_VARNAME_TOO_LONG		   "invalid varname length %ld: max %d"
#define YDBPY_ERR_SEQUENCE_TOO_LONG		   "invalid sequence length %ld: max %d"
#define YDBPY_ERR_BYTES_TOO_LONG		   "invalid bytes length %ld: max %d"
#define YDBPY_ERR_KEY_IN_SEQUENCE_INCORRECT_LENGTH "item %lu must be length 1 or 2."
#define YDBPY_ERR_KEY_IN_SEQUENCE_VARNAME_TOO_LONG "item %ld in key sequence has invalid varname length %ld: max %d."

#define YDBPY_ERR_KEY_IN_SEQUENCE_SUBSARRAY_INVALID "item %ld in key sequence has invalid subsarray: %s"

#define YDBPY_ERR_VARNAME_INVALID   "'varnames' argument invalid: %s"
#define YDBPY_ERR_SUBSARRAY_INVALID "'subsarray' argument invalid: %s"
#define YDBPY_ERR_KEYS_INVALID	    "'keys' argument invalid: %s"

// Prevents compiler warnings for variables used only in asserts
#define UNUSED(x) (void)(x)

typedef struct {
	ydb_buffer_t *varname;
	int	      subs_used;
	ydb_buffer_t *subsarray;
} YDBKey;

#define YDB_COPY_BYTES_TO_BUFFER(BYTES, BYTES_LEN, BUFFERP, COPY_DONE) \
	{                                                              \
		if (BYTES_LEN <= (BUFFERP)->len_alloc) {               \
			memcpy((BUFFERP)->buf_addr, BYTES, BYTES_LEN); \
			(BUFFERP)->len_used = BYTES_LEN;               \
			COPY_DONE = TRUE;                              \
		} else {                                               \
			COPY_DONE = FALSE;                             \
		}                                                      \
	}

#define POPULATE_NEW_BUFFER(PYVARNAME, YDBVARNAME, VARNAMELEN, FUNCTIONNAME, RETURN_NULL)                      \
	{                                                                                                      \
		bool copy_success;                                                                             \
		YDB_MALLOC_BUFFER(&(YDBVARNAME), (VARNAMELEN));                                                \
		YDB_COPY_BYTES_TO_BUFFER((PYVARNAME), (VARNAMELEN), &(YDBVARNAME), copy_success);              \
		if (!copy_success) {                                                                           \
			PyErr_Format(YDBPythonError, "YDB_COPY_BYTES_TO_BUFFER failed in %s", (FUNCTIONNAME)); \
			(RETURN_NULL) = true;                                                                  \
		}                                                                                              \
	}

#define POPULATE_SUBS_USED_AND_SUBSARRAY(SUBSARRAY_PY, SUBSUSED, SUBSARRAY_YDB, RETURN_NULL)                      \
	{                                                                                                         \
		bool success = true;                                                                              \
                                                                                                                  \
		SUBSUSED = 0;                                                                                     \
		SUBSARRAY_YDB = NULL;                                                                             \
		if (Py_None != SUBSARRAY_PY) {                                                                    \
			SUBSUSED = PySequence_Length(SUBSARRAY_PY);                                               \
			SUBSARRAY_YDB = (ydb_buffer_t *)calloc(SUBSUSED, sizeof(ydb_buffer_t));                   \
			success = convert_py_sequence_to_ydb_buffer_array(SUBSARRAY_PY, SUBSUSED, SUBSARRAY_YDB); \
			if (!success)                                                                             \
				RETURN_NULL = true;                                                               \
		}                                                                                                 \
	}

#define FREE_BUFFER_ARRAY(ARRAY, LEN)                                 \
	{                                                             \
		for (int i = 0; i < (LEN); i++) {                     \
			YDB_FREE_BUFFER(&((ydb_buffer_t *)ARRAY)[i]); \
		}                                                     \
		free(ARRAY);                                          \
	}

/* Safely downcasts SRC_LEN (Py_ssize_t) and stores in DEST_LEN (unsigned int).
 *
 * First checks that the value of SRC_LEN is within bounds of the YDB limit signaled
 * by IS_VARNAME, i.e. YDB_MAX_IDENT for variable names (IS_VARNAME) or YDB_MAX_STR
 * for string values (!IS_VARNAME). If this check succeeds, Py_SAFE_DOWNCAST is invoked.
 * Otherwise, a Python ValueError is raised.
 */
#define INVOKE_PY_SAFE_DOWNCAST(DEST_LEN, SRC_LEN, IS_VARNAME)                                        \
	{                                                                                             \
		Py_ssize_t max_len, src_len;                                                          \
		char *	   err_msg;                                                                   \
                                                                                                      \
		src_len = SRC_LEN;                                                                    \
		if (IS_VARNAME) {                                                                     \
			max_len = YDB_MAX_IDENT;                                                      \
			err_msg = YDBPY_ERR_VARNAME_TOO_LONG;                                         \
		} else {                                                                              \
			max_len = YDB_MAX_STR;                                                        \
			err_msg = YDBPY_ERR_BYTES_TOO_LONG;                                           \
		}                                                                                     \
		if (max_len < (src_len)) {                                                            \
			raise_ValidationError(YDBPython_ValueError, NULL, err_msg, src_len, max_len); \
			return NULL;                                                                  \
		} else {                                                                              \
			DEST_LEN = Py_SAFE_DOWNCAST(src_len, Py_ssize_t, unsigned int);               \
		}                                                                                     \
	}

#define RETURN_IF_INVALID_SEQUENCE(SEQUENCE, SEQUENCE_TYPE)              \
	{                                                                \
		if (!is_valid_sequence(SEQUENCE, SEQUENCE_TYPE, NULL)) { \
			return NULL;                                     \
		}                                                        \
	}

#define FIX_BUFFER_LENGTH(BUFFER)                           \
	{                                                   \
		int correct_length = BUFFER.len_used;       \
                                                            \
		YDB_FREE_BUFFER(&BUFFER);                   \
		YDB_MALLOC_BUFFER(&BUFFER, correct_length); \
	}

/* PYTHON EXCEPTION DECLARATIONS */

/* YottaDBError represents an error return status from any of the libyottadb
 * functions being wrapped. Since YottaDB returns a status that is a number and
 * has a way to create a message from that number the choice was to preserve
 * both in the python exception. This means we need to extend the exception to
 * accept both. Use raise_YottaDBError function to raise
 */
static PyObject *YDBException;
static PyObject *YDBError;

static PyObject *YDBTPException;
static PyObject *YDBTPRollback;
static PyObject *YDBTPRestart;

/* YottaDBLockTimeout is a simple exception to indicate that a lock failed due
 * to timeout. */
static PyObject *YDBTimeoutError;

/* YDBPythonError is to be raised when there is a possibility for an error to
   occur but that we believe that it should never happen. */
static PyObject *YDBPythonError;
