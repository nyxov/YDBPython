/* A structure that represents a key using ydb c types. used internally for converting between python and ydb c types */
typedef struct {
    ydb_buffer_t *varname;
    int subs_used;
    ydb_buffer_t *subsarray;
} YDBKey;

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
static PyObject *YottaDBError;

/* YottaDBLockTimeout is a simple exception to indicate that a lock failed due to timeout. */
static PyObject *YottaDBLockTimeout;

/* YDBPythonBugError is to be raised when there is a posobility for an error to occur but that we believe that it should never happen. */
static PyObject *YDBPythonBugError;