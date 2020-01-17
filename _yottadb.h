/* A structure that represents a key using ydb c types. used internally for converting between python and ydb c types */
typedef struct
{
	ydb_buffer_t *varname;
	int subs_used;
	ydb_buffer_t *subsarray;
} YDBKey;

/*  */
#define	LOAD_BUFFER(BUFFERP, STR, LEN)	\
{										\
	(BUFFERP)->len_alloc = LEN;			\
	(BUFFERP)->len_used = LEN;			\
	(BUFFERP)->buf_addr = STR;			\
}

/* CALL_WRAP_2 through CALL_WRAP_5 are macros to set up the call to the 2 similar api functions. The number at the end
 * represents the number of elements that the simple api call has and that the 2 have in common.
 *
 *	Parameters:
 *		API			-	the variable that containes the SIMPLE/SIMPLE_THREADED flag for the unified function (usually 'api')
 *		SFUNC		-	the function for the simple api
 *		STFUNC		-	the function for the simple threaded api
 *		TPTOKEN		-	the variable containing the TPTOKEN value (usually 'tp_token')
 *		ERRBUF		-	the variable containing the error buffer ydb_buffer_t (usually 'error_string_buffer')
 *		ONE - FIVE	-	the ordered arguments that the simple and simple threaded api's share
 *		RETSTATUS	-	the variable that the return status will be assigned to
 */
#define CALL_WRAP_2(THREADED, SFUNC, STFUNC, TPTOKEN, ERRBUF,  ONE, TWO, RETSTATUS) 								\
{																											\
	if ((THREADED))																			\
		(RETSTATUS) = (STFUNC)((TPTOKEN), (ERRBUF), (ONE), (TWO));											\
	else																				\
		(RETSTATUS) = (SFUNC)((ONE), (TWO));																\
}

#define CALL_WRAP_3(THREADED, SFUNC, STFUNC, TPTOKEN, ERRBUF, ONE, TWO, THREE, RETSTATUS) 						\
{																											\
	if (THREADED)																			\
		(RETSTATUS) = (STFUNC)((TPTOKEN), (ERRBUF), (ONE), (TWO), (THREE));									\
	else                                                                                                    \
		(RETSTATUS) = (SFUNC)((ONE), (TWO), (THREE));														\
}

#define CALL_WRAP_4(THREADED, SFUNC, STFUNC, TPTOKEN, ERRBUF, ONE, TWO, THREE, FOUR, RETSTATUS) 					\
{																											\
	if (THREADED)																			\
		(RETSTATUS) = (STFUNC)((TPTOKEN), (ERRBUF), (ONE), (TWO), (THREE), (FOUR));							\
	else                                                                                                    \
		(RETSTATUS) = (SFUNC)((ONE), (TWO), (THREE), (FOUR));												\
}

#define CALL_WRAP_5(THREADED, SFUNC, STFUNC, TPTOKEN, ERRBUF, ONE, TWO, THREE, FOUR, FIVE,  RETSTATUS) 			\
{																											\
	if (THREADED)																			\
		(RETSTATUS) = (STFUNC)((TPTOKEN), (ERRBUF), (ONE), (TWO), (THREE), (FOUR), (FIVE));					\
	else                                                                                                    \
		(RETSTATUS) = (SFUNC)((ONE), (TWO), (THREE), (FOUR), (FIVE));										\
}

#define SETUP_SUBS(SUBSARRAY_PY, SUBSUSED, SUBSARRAY_YDB)								\
{																						\
	SUBSUSED = 0;																		\
	SUBSARRAY_YDB = NULL;																\
	if (SUBSARRAY_PY != Py_None)														\
	{																					\
		SUBSUSED = PySequence_Length(SUBSARRAY_PY);										\
		SUBSARRAY_YDB = convert_py_bytes_sequence_to_ydb_buffer_array(SUBSARRAY_PY);	\
	}																					\
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

