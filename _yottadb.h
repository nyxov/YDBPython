/****************************************************************
 *                                                              *
 * Copyright (c) 2020 Peter Goss All rights reserved.           *
 *                                                              *
 * Copyright (c) 2020 YottaDB LLC and/or its subsidiaries.      *
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

#define YDBPY_DEFAULT_VALUE_LEN		256
#define YDBPY_DEFAULT_SUBSCRIPT_LEN	32
#define MAX_CONONICAL_NUMBER_STRING_MAX 48

#define YDB_LOCK_ST_INIT_ARG_NUMS    4
#define YDB_LOCK_ST_NUM_ARGS_PER_KEY 3

#define YDBPY_VALID 0

#define YDBPY_MAX_ERRORMSG    1024
#define YDBPY_MAX_REASON      YDBPY_MAX_ERRORMSG / 4
#define YDBPY_TYPE_ERROR_MAX  -100
#define YDBPY_TYPE_ERROR_MIN  -199
#define YDBPY_VALUE_ERROR_MAX -200
#define YDBPY_VALUE_ERROR_MIN -299

#define YDBPY_INVALID_NOT_LIST_OR_TUPLE -101
#define YDBPY_ERRMSG_NOT_LIST_OR_TUPLE	"value must be list or tuple."

#define YDBPY_INVALID_ITEM_IN_SEQUENCE_NOT_BYTES -102
#define YDBPY_ERRMSG_ITEM_IN_SEQUENCE_NOT_BYTES	 "item %ld is not of type 'bytes'"

#define YDBPY_INVALID_KEY_IN_SEQUENCE_NOT_LIST_OR_TUPLE -103
#define YDBPY_ERRMSG_KEY_IN_SEQUENCE_NOT_LIST_OR_TUPLE	"item %ld is not a list or tuple."

#define YDBPY_INVALID_KEY_IN_SEQUENCE_VARNAME_NOT_BYTES -104
#define YDBPY_ERRMSG_KEY_IN_SEQUENCE_VARNAME_NOT_BYTES	"item %ld in key sequence invalid: first element must be of type 'bytes'"

#define YDBPY_INVALID_VARNAME_TOO_LONG -201
#define YDBPY_ERRMSG_VARNAME_TOO_LONG  "invalid varname length %ld: max %d"

#define YDBPY_INVALID_SEQUENCE_TOO_LONG -202
#define YDBPY_ERRMSG_SEQUENCE_TOO_LONG	"invalid sequence length %ld: max %d"

#define YDBPY_INVALID_BYTES_TOO_LONG -203
#define YDBPY_ERRMSG_BYTES_TOO_LONG  "invalid bytes length %ld: max %d"
#define YDBPY_ERRMSG_BYTES_TOO_LONG2 "invalid bytes length %ld: max %u"

#define YDBPY_INVALID_KEY_IN_SEQUENCE_INCORECT_LENGTH -204
#define YDBPY_ERRMSG_KEY_IN_SEQUENCE_INCORECT_LENGTH  "item %ld must be length 1 or 2."

#define YDBPY_INVALID_KEY_IN_SEQUENCE_VARNAME_TOO_LONG -205
#define YDBPY_ERRMSG_KEY_IN_SEQUENCE_VARNAME_TOO_LONG  "item %ld in key sequence has invalid varname length %ld: max %d."

#define YDBPY_ERRMSG_KEY_IN_SEQUENCE_SUBSARRAY_INVALID "item %ld in key sequence has invalid subsarray: %s"

#define YDBPY_ERRMSG_VARNAME_INVALID   "'varnames' argument invalid: %s"
#define YDBPY_ERRMSG_SUBSARRAY_INVALID "'subsarray' argument invalid: %s"
#define YDBPY_ERRMSG_KEYS_INVALID      "'keys' argument invalid: %s"

#define FORMAT_ERROR_MESSAGE(...) snprintf(error_message, YDBPY_MAX_REASON, __VA_ARGS__);

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

#define POPULATE_SUBS_USED_AND_SUBSARRAY(SUBSARRAY_PY, SUBSUSED, SUBSARRAY_YDB, RETURN_NULL)                            \
	{                                                                                                               \
		bool success = true;                                                                                    \
		SUBSUSED = 0;                                                                                           \
		SUBSARRAY_YDB = NULL;                                                                                   \
		if (Py_None != SUBSARRAY_PY) {                                                                          \
			SUBSUSED = PySequence_Length(SUBSARRAY_PY);                                                     \
			SUBSARRAY_YDB = (ydb_buffer_t *)calloc(SUBSUSED, sizeof(ydb_buffer_t));                         \
			success = convert_py_bytes_sequence_to_ydb_buffer_array(SUBSARRAY_PY, SUBSUSED, SUBSARRAY_YDB); \
			if (!success)                                                                                   \
				RETURN_NULL = true;                                                                     \
		}                                                                                                       \
	}

#define FREE_BUFFER_ARRAY(ARRAY, LEN)                                 \
	{                                                             \
		for (int i = 0; i < (LEN); i++)                       \
			YDB_FREE_BUFFER(&((ydb_buffer_t *)ARRAY)[i]); \
	}

#define VALIDATE_AND_CONVERT_BYTES_LEN(ORIGINAL_LEN, CONVERTED_LEN, MAX_LEN, LEN_ERR, LEN_ERR_MSG)                  \
	{                                                                                                           \
		if ((MAX_LEN) < (ORIGINAL_LEN)) {                                                                   \
			char validation_error_message[YDBPY_MAX_ERRORMSG];                                          \
			snprintf(validation_error_message, YDBPY_MAX_ERRORMSG, LEN_ERR_MSG, ORIGINAL_LEN, MAX_LEN); \
			raise_ValidationError(LEN_ERR, validation_error_message);                                   \
			return NULL;                                                                                \
		} else {                                                                                            \
			CONVERTED_LEN = Py_SAFE_DOWNCAST(ORIGINAL_LEN, Py_ssize_t, unsigned int);                   \
		}                                                                                                   \
	}

#define VALIDATE_SEQUENCE_OF_BYTES_INPUT(SEQUENCE, MAX_SEQUENCE_LEN, MAX_BYTES_LEN, OUTER_ERROR_MESSAGE)                        \
	{                                                                                                                       \
		if (Py_None != SEQUENCE) { /* allow None */                                                                     \
			char validation_reason_message[YDBPY_MAX_REASON];                                                       \
			int  validation_status                                                                                  \
			    = validate_sequence_of_bytes(SEQUENCE, MAX_SEQUENCE_LEN, MAX_BYTES_LEN, validation_reason_message); \
			if (YDBPY_VALID != validation_status) {                                                                 \
				char validation_error_message[YDBPY_MAX_ERRORMSG];                                              \
				snprintf(validation_error_message, YDBPY_MAX_ERRORMSG, OUTER_ERROR_MESSAGE,                     \
					 validation_reason_message);                                                            \
				raise_ValidationError(validation_status, validation_error_message);                             \
				return NULL;                                                                                    \
			}                                                                                                       \
		}                                                                                                               \
	}

#define VALIDATE_SUBSARRAY(SUBSARRAY) \
	{ VALIDATE_SEQUENCE_OF_BYTES_INPUT(SUBSARRAY, YDB_MAX_SUBS, YDB_MAX_STR, YDBPY_ERRMSG_SUBSARRAY_INVALID) }

#define VALIDATE_VARNAMES(VARNAMES) \
	{ VALIDATE_SEQUENCE_OF_BYTES_INPUT(VARNAMES, YDB_MAX_NAMES, YDB_MAX_IDENT, YDBPY_ERRMSG_VARNAME_INVALID) }

#define FIX_BUFFER_LENGTH(BUFFER)                           \
	{                                                   \
		int correct_length = BUFFER.len_used;       \
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