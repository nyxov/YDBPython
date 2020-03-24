/* A structure that represents a key using ydb c types. used internally for converting between python and ydb c types */
#define TEMP_YDB_RAISE_PYTHON_EXCEPTION -2 // TODO: remove after resolution of YDB issue #548
#define INT32_TO_STRING_MAX 12

typedef struct {
    ydb_buffer_t *varname;
    int subs_used;
    ydb_buffer_t *subsarray;
} YDBKey;

#define YDB_COPY_BYTES_TO_BUFFER(BYTES, BYTES_LEN, BUFFERP, COPY_DONE) {                 \
    if (BYTES_LEN <= (BUFFERP)->len_alloc) {                                             \
        memcpy((BUFFERP)->buf_addr, BYTES, BYTES_LEN);                                   \
        (BUFFERP)->len_used = BYTES_LEN;                                                 \
        COPY_DONE = TRUE;                                                                \
    } else {                                                                             \
        COPY_DONE = FALSE;                                                               \
    }                                                                                    \
}

#define SETUP_BUFFER(PYVARNAME, YDBVARNAME, VARNAMELEN, FUNCTIONNAME, RETURN_NULL) {            \
    bool copy_success;                                                                          \
    YDB_MALLOC_BUFFER(&(YDBVARNAME), (VARNAMELEN));                                             \
    YDB_COPY_BYTES_TO_BUFFER((PYVARNAME), (VARNAMELEN), &(YDBVARNAME), copy_success);           \
    if (!copy_success) {                                                                        \
        PyErr_Format(YDBPythonError, "YDB_COPY_BYTES_TO_BUFFER failed in %s", (FUNCTIONNAME));  \
        (RETURN_NULL) = true;                                                                   \
    }                                                                                           \
}

#define SETUP_SUBS(SUBSARRAY_PY, SUBSUSED, SUBSARRAY_YDB, RETURN_NULL) {                                    \
    bool success = true;                                                                                    \
    SUBSUSED = 0;                                                                                           \
    SUBSARRAY_YDB = NULL;                                                                                   \
    if (Py_None != SUBSARRAY_PY) {                                                                          \
        SUBSUSED = PySequence_Length(SUBSARRAY_PY);                                                         \
        SUBSARRAY_YDB = (ydb_buffer_t*)calloc(SUBSUSED, sizeof(ydb_buffer_t));                              \
        success = convert_py_bytes_sequence_to_ydb_buffer_array(SUBSARRAY_PY, SUBSUSED, SUBSARRAY_YDB);     \
        if (!success)                                                                                       \
            RETURN_NULL = true;                                                                             \
    }                                                                                                       \
}


#define FREE_BUFFER_ARRAY(ARRAY, LEN) {                                                  \
    for(int i = 0; i < (LEN); i++)                                                       \
        YDB_FREE_BUFFER(&((ydb_buffer_t*)ARRAY)[i]);                                     \
}

/* PYTHON EXCEPTION DECLAIRATIONS */

/* YottaDBError represents an error return status from any of the libyottadb functions being wrapped.
 * Since YottaDB returns a status that is a number and has a way to create a message from that number
 * the choice was to preserve both in the python exception. This means we need to extend the exception
 * to accept both. Use raise_YottaDBError function to raise
 */
static PyObject *YDBError;
static PyObject *YDBTPRollback;

/* YottaDBLockTimeout is a simple exception to indicate that a lock failed due to timeout. */
static PyObject *YDBTimeoutError;

/* YDBPythonError is to be raised when there is a posobility for an error to occur but that we believe that it should never happen. */
static PyObject *YDBPythonError;