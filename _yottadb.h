/* A structure that represents a key using ydb c types. used internally for converting between python and ydb c types */
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


#define SETUP_SUBS(SUBSARRAY_PY, SUBSUSED, SUBSARRAY_YDB) {                              \
    SUBSUSED = 0;                                                                        \
    SUBSARRAY_YDB = NULL;                                                                \
    if (Py_None != SUBSARRAY_PY) {                                                       \
        SUBSUSED = PySequence_Length(SUBSARRAY_PY);                                      \
        SUBSARRAY_YDB = convert_py_bytes_sequence_to_ydb_buffer_array(SUBSARRAY_PY);     \
    }                                                                                    \
}

/* PYTHON EXCEPTION DECLAIRATIONS */

/* YottaDBError represents an error return status from any of the libyottadb functions being wrapped.
 * Since YottaDB returns a status that is a number and has a way to create a message from that number
 * the choice was to preserve both in the python exception. This means we need to extend the exception
 * to accept both. Use raise_YottaDBError function to raise
 */
static PyObject *YDBError;

/* YottaDBLockTimeout is a simple exception to indicate that a lock failed due to timeout. */
static PyObject *YDBTimeoutError;

/* YDBPythonError is to be raised when there is a posobility for an error to occur but that we believe that it should never happen. */
static PyObject *YDBPythonError;