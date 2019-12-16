/****************************************************************
 *                                                              *
 * Copyright (c) 2019 Peter Goss All rights reserved.           *
 *                                                              *
 * Copyright (c) 2019 YottaDB LLC and/or its subsidiaries.      *
 * All rights reserved.                                         *
 *                                                              *
 *	This source code contains the intellectual property         *
 *	of its copyright holder(s), and is made available           *
 *	under a license.  If you do not know the terms of           *
 *	the license, please stop and do not read further.           *
 *                                                              *
 ****************************************************************/


#include <stdbool.h>
#include <Python.h>
#include <libyottadb.h>
#include <ffi.h>

/* TYPEDEFS */

/* Flag type to indicate if _s or _st function was called. Used internally to unify wrappers for for _s and _st functions */
typedef enum
{
	SIMPLE,
	SIMPLE_THREADED
} api_type;

/* A structure that represents a key using ydb c types. used internally for converting between python and ydb c types */
typedef struct
{
	ydb_buffer_t *varname;
	int subs_used;
	ydb_buffer_t *subsarray; //array
} key_ydb;

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
#define CALL_WRAP_2(API, SFUNC, STFUNC, TPTOKEN, ERRBUF,  ONE, TWO, RETSTATUS) 								\
{																											\
	if ( (API) == SIMPLE_THREADED)																			\
		(RETSTATUS) = (STFUNC)((TPTOKEN), (ERRBUF), (ONE), (TWO));											\
	else if ((API) == SIMPLE)																				\
		(RETSTATUS) = (SFUNC)((ONE), (TWO));																\
}

#define CALL_WRAP_3(API, SFUNC, STFUNC, TPTOKEN, ERRBUF, ONE, TWO, THREE, RETSTATUS) 						\
{																											\
	if ((API) == SIMPLE_THREADED)																			\
		(RETSTATUS) = (STFUNC)((TPTOKEN), (ERRBUF), (ONE), (TWO), (THREE));									\
	else if ((API) == SIMPLE)																				\
		(RETSTATUS) = (SFUNC)((ONE), (TWO), (THREE));														\
}

#define CALL_WRAP_4(API, SFUNC, STFUNC, TPTOKEN, ERRBUF, ONE, TWO, THREE, FOUR, RETSTATUS) 					\
{																											\
	if ( (API) == SIMPLE_THREADED)																			\
		(RETSTATUS) = (STFUNC)((TPTOKEN), (ERRBUF), (ONE), (TWO), (THREE), (FOUR));							\
	else if ((API) == SIMPLE)																				\
		(RETSTATUS) = (SFUNC)((ONE), (TWO), (THREE), (FOUR));												\
}

#define CALL_WRAP_5(API, SFUNC, STFUNC, TPTOKEN, ERRBUF, ONE, TWO, THREE, FOUR, FIVE,  RETSTATUS) 			\
{																											\
	if ((API) == SIMPLE_THREADED)																			\
		(RETSTATUS) = (STFUNC)((TPTOKEN), (ERRBUF), (ONE), (TWO), (THREE), (FOUR), (FIVE));					\
	else if ((API) == SIMPLE)																				\
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

/* function that sets up Exception to have both a code and a message */
PyObject* make_getter_code()
{
	const char *code;
	PyObject *d, *output;

	code =
	"@property\n"
	"def code(self):\n"
	"  try:\n"
	"    return self.args[0]\n"
	"  except IndexError:\n"
	"    return -1\n"
	"@property\n"
	"def message(self):\n"
	"  try:\n"
	"    return self.args[1]\n"
	"  except IndexError:\n"
	"    return ''\n"
	"\n";

	d = PyDict_New();
	PyDict_SetItemString(d, "__builtins__", PyEval_GetBuiltins());
	output = PyRun_String(code,Py_file_input,d,d);
	if (output==NULL)
	{
		Py_DECREF(d);
		return NULL;
	}
	Py_DECREF(output);
	PyDict_DelItemString(d,"__builtins__"); /* __builtins__ should not be an attribute of the exception */

	return d;
}

/* YottaDBLockTimeout is a simple exception to indicate that a lock failed due to timeout. */
static PyObject *YottaDBLockTimeout;


/* LOCAL UTILITY FUNCTIONS */

/* YDB_BUFFER_T UTILITIES */
/* Routine to create an empty ydb_buffer_t
 *
 * Parameters:
 *   len	- the length of the string to allocate for the ydb_buffer_t
 *
 * Use YDB_FREE_BUFFER to free.
 */
static ydb_buffer_t* empty_buffer(int len)
{
	ydb_buffer_t *ret_buffer;

	ret_buffer = (ydb_buffer_t*)malloc(len*sizeof(ydb_buffer_t));
	YDB_MALLOC_BUFFER(ret_buffer, len);

	return ret_buffer;
}

/* Routine to create an ydb_buffer_t from a string and a length
 *
 * Parameters:
 *   str	- the string to copy into a new ydb_buffer_t
 *   len	- the length of the string
 *
 * Do not free if string was created by Python functions such as PyArg_ParseTupleAndKeywords (will result in a double free)
 */
static ydb_buffer_t* convert_str_to_buffer(char *str, int len)
{
	char *copy;
	ydb_buffer_t *ret_buffer;

	ret_buffer = (ydb_buffer_t*)malloc(sizeof(ydb_buffer_t));
	copy = (char*)malloc(sizeof(char)*len);
	strncpy(copy, str, len);
	LOAD_BUFFER(ret_buffer, copy, len);

	return ret_buffer;
}

/* ARRAY OF YDB_BUFFER_T UTILITIES */

/* Routine to create an array of empty ydb_buffer_ts with num elements each with an allocated length of len
 *
 * Parameters:
 *   num	- the number of buffers to allocate in the array
 *   len	- the length of the string to allocate for each of the the ydb_buffer_ts
 *
 * free with free_buffer_array function below
 */
static ydb_buffer_t* empty_buffer_array(int num, int len)
{
	int i;
	ydb_buffer_t *return_buffer_array;

	return_buffer_array = (ydb_buffer_t*)malloc((num) * sizeof(ydb_buffer_t));
	for(i = 0; i < num; i++)
		YDB_MALLOC_BUFFER(&return_buffer_array[i], len);

	return return_buffer_array;
}

/* Routine to free an array of ydb_buffer_ts
 *
 * Parameters:
 *   array 	- pointer to the array of ydb_buffer_ts to be freed.
 *	 len	- number of elements in the array to be freed
 *
 */
static void free_buffer_array(ydb_buffer_t *array, int len)
{
	for(int i = 0; i < len; i++)
		YDB_FREE_BUFFER(&((ydb_buffer_t*)array)[0]);
}

/* UTILITIES TO CONVERT BETWEEN PYUNICODE STRING OBJECTS AND YDB_BUFFER_TS */

/* Routine to convert from a Python bytes object to a ydb_buffer_t
 *
 * Parameters:
 *   bytes	- a python bytes object
 *
 * this function copies the bytes string from the PyBytes object so you should free it with YDB_FREE_BUFFER
 */
static ydb_buffer_t* convert_py_bytes_to_ydb_buffer_t(PyObject *bytes)
{
	int len;
	char* bytes_c;

	len = PyBytes_Size(bytes);
	bytes_c = PyBytes_AsString(bytes);

	return convert_str_to_buffer(bytes_c, len);
}

/* UTILITIES TO CONVERT BETWEEN SEQUENCES OF PYUNICODE STRING OBJECTS AND AN ARRAY OF YDB_BUFFER_TS */

/* Routine to validate that the PyObject passed to it is indeed an array of python bytes objects.
 *
 * Parameters:
 *   sequence	- the python object to check.
 */
static bool validate_sequence_of_bytes(PyObject *sequence)
{
	int i;
	PyObject *item;

	if (!PySequence_Check(sequence))
		return false;

	if (PyBytes_Check(sequence)) /* PyBytes it's self is a sequence */
		return false;

	for (i=0; i<PySequence_Length(sequence); i++) /* check each item for a bytes object */
	{
		item = PySequence_GetItem(sequence, i);
		if (!PyBytes_Check(item)) {
			Py_DECREF(item);
			return false;
		}
		Py_DECREF(item);
	}
	return true;
}

/* Rutine to validate a 'subsarray' argument in many of the wrapped functions. The 'subsarray' argument must be a sequence
 * of Python bytes objects or a Python None. Will set Exception String and return 'false' if invalid and return 'true' otherwise.
 * (Calling function is expected to return NULL to cause the Exception to be raised.)
 *
 * Parameters:
 *   subsarray	- the Python object to validate.
 */
static bool validate_subsarray_object(PyObject *subsarray)
{
	 if (subsarray != Py_None) /* don't check if None */
	 {
		if (!validate_sequence_of_bytes(subsarray))
		{
			/* raise TypeError */
			PyErr_SetString(PyExc_TypeError, "'subsarray' must be a Sequence (e.g. List or Tuple) containing only bytes or None");
			return false;
		}
	}
	return true;
}

/* Routine to convert a sequence of Python bytes into a C array of ydb_buffer_ts. Routine assumes sequence was already
 * validated with 'validate_sequence_of_bytes' function. The function creates a copy of each Python bytes' data so
 * the resulting array should be freed by using the 'free_buffer_array' function.
 *
 * Parameters:
 *    sequence	- a Python Object that is expected to be a Python Sequence containing Strings.
 */
ydb_buffer_t* convert_py_bytes_sequence_to_ydb_buffer_array(PyObject *sequence)
{
	int num, len;
	char *str_c, *bytes_c;
	PyObject *bytes;
	ydb_buffer_t *return_buffer_array;

	num = PySequence_Length(sequence);
	return_buffer_array = (ydb_buffer_t*)malloc((num) * sizeof(ydb_buffer_t));
	for(int i = 0; i < num; i++)
	{
		bytes = PySequence_GetItem(sequence, i);
		len = PyBytes_Size(bytes);
		bytes_c = PyBytes_AsString(bytes);
		str_c = (char*)malloc(sizeof(char)*(len));
		strncpy(str_c, bytes_c, len);
		LOAD_BUFFER(&return_buffer_array[i], str_c, len);
		Py_DECREF(bytes);
	}
	return return_buffer_array;
}

/* converts an array of ydb_buffer_ts into a sequence (Tuple) of Python strings.
 *
 * Parameters:
 *    buffer_array		- a C array of ydb_buffer_ts
 *    len				- the length of the above array
 */
PyObject* convert_ydb_buffer_array_to_py_tuple(ydb_buffer_t *buffer_array, int len)
{
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
 *    dest		- pointer to the key_ydb to fill.
 *    varname	- Python Bytes object representing the varname
 *    subsarray	- array of python Bytes objects representing the array of subcripts
 */

static void load_key_ydb(key_ydb *dest, PyObject *varname, PyObject *subsarray)
{
	dest->varname = convert_py_bytes_to_ydb_buffer_t(varname);
	if (subsarray != Py_None)
	{
		dest->subs_used = PySequence_Length(subsarray);
		dest->subsarray = convert_py_bytes_sequence_to_ydb_buffer_array(subsarray);
	} else
		dest->subs_used = 0;
}

/* Routine to free a key_ydb structure.
 *
 * Parameters:
 *    key	- pointer to the key_ydb to free.
 */
static void free_key_ydb(key_ydb* key)
{
	int i;

	YDB_FREE_BUFFER((key->varname));
	for (i=0; i<key->subs_used; i++)
		YDB_FREE_BUFFER(&((ydb_buffer_t*)key->subsarray)[i]);
}

/* Routine to validate a sequence of Python sequences representing keys. (Used only by lock().)
 * Validation rule:
 *		1) key_sequence must be a sequence
 *		2) each item in key_sequence must be a sequence
 *		3) each item must be a sequence of 1 or 2 sub-items.
 *		4) item[0] must be a bytes object.
 *		5) item[1] either does not exist, is None or a sequence
 *		6) if item[1] is a sequence then it must contain only bytes objects.
 *
 * Parameters:
 *    keys_sequence		- a Python object that is to be validated.
 */
static bool validate_py_keys_sequence_bytes(PyObject* keys_sequence)
{
	int i, keys_len;
	PyObject *key, *varname, *subsarray;

	if (!PySequence_Check(keys_sequence))
	{
		PyErr_SetString(PyExc_TypeError, "'keys' argument must be a Sequence (e.g. List or Tuple) containing  sequences of 2 values the first being a bytes object(varname) and the following being a sequence of bytes objects(subsarray)");
		return false;
	}
	keys_len = PySequence_Length(keys_sequence);
	for (i=0; i < keys_len; i++)
	{
		key = PySequence_GetItem(keys_sequence, i);
		if (!PySequence_Check(key))
		{
			PyErr_Format(PyExc_TypeError, "item %d in 'keys' sequence is not a sequence", i);
			Py_DECREF(key);
			return false;
		}
		if (PySequence_Length(key) != 1 && PySequence_Length(key) != 2)
		{
			PyErr_Format(PyExc_TypeError, "item %d in 'keys' sequence is not a sequence of 1 or 2 items", i);
			Py_DECREF(key);
			return false;
		}

		varname = PySequence_GetItem(key, 0);
		if (!PyBytes_Check(varname))
		{
			PyErr_Format(PyExc_TypeError, "the first value in item %d of 'keys' sequence must be a bytes object", i);
			Py_DECREF(key);
			Py_DECREF(varname);
			return false;
		}
		Py_DECREF(varname);

		if (PySequence_Length(key) == 2)
		{
			subsarray = PySequence_GetItem(key, 1);
			if (!validate_subsarray_object(subsarray))
			{ /* overwrite Exception string set by 'validate_subsarray_object' to be appropriate for lock context */
				PyErr_Format(PyExc_TypeError, "the second value in item %d of 'keys' sequence must be a sequence of bytes or None", i);
				Py_DECREF(key);
				Py_DECREF(subsarray);
				return false;
			}
			Py_DECREF(subsarray);
		}
		Py_DECREF(key);

	}
	return true;
}

/* Routine to covert a sequence of keys in Python sequences and bytes to an array of key_ydbs. Assumes that the input
 * has already been validated with 'validate_py_keys_sequence' above. Use 'free_key_ydb_array' below to free the returned
 * value.
 *
 * Parameters:
 *    sequence	- a Python object that has already been validated with 'validate_py_keys_sequence' or equivalent.
 */
static key_ydb* convert_key_sequence_to_key_ydb_array(PyObject* sequence)
{
	int i, keys_len;
	PyObject *key, *varname, *subsarray;
	key_ydb *ret_keys;
	keys_len = PySequence_Length(sequence);
	ret_keys = (key_ydb*)malloc(keys_len * sizeof(key_ydb));
	for (i=0; i< keys_len; i++)
	{
		key = PySequence_GetItem(sequence, i);
		varname = PySequence_GetItem(key, 0);
		subsarray = Py_None;
		if (PySequence_Length(key) == 2)
			subsarray = PySequence_GetItem(key, 1);
		load_key_ydb(&ret_keys[i], varname, subsarray);
		Py_DECREF(key);
		Py_DECREF(subsarray);
	}
	return ret_keys;
}

/* Routine to free an array of key_ydbs as returned by above 'convert_key_sequence_to_key_ydb_array'.
 *
 * Parameters:
 *    keysarray	- the array that is to be freed.
 *    len		- the number of elements in keysarray.
 */
static void free_key_ydb_array(key_ydb* keysarray, int len)
{
	int i;
	for(i = 0; i < len; i++)
		free_key_ydb(&keysarray[i]);
}

/* Routine to help raise a YottaDBError. The caller still needs to return NULL for the Exception to be raised.
 * This routine will check if the message has been set in the error_string_buffer and look it up if it has not been.
 *
 * Parameters:
 *    status				- the error code that is returned by the wrapped ydb_ function.
 *    error_string_buffer	- a ydb_buffer_t that may or may not contain the error message.
 */
static void raise_YottaDBError(int status, ydb_buffer_t* error_string_buffer)
{
	int msg_status;
	ydb_buffer_t *ignored_buffer;
	PyObject *tuple;

	if (error_string_buffer->len_used == 0)
	{
		msg_status = ydb_message(status, error_string_buffer);
		if (msg_status == YDB_ERR_SIMPLEAPINOTALLOWED)
		{
			ignored_buffer = empty_buffer(YDB_MAX_ERRORMSG);
			ydb_message_t(YDB_NOTTP, ignored_buffer, status, error_string_buffer);
			YDB_FREE_BUFFER(ignored_buffer);
		}
	}
	tuple = PyTuple_New(2);
	PyTuple_SetItem(tuple, 0, PyLong_FromLong(status));
	PyTuple_SetItem(tuple, 1, Py_BuildValue("s#", error_string_buffer->buf_addr, error_string_buffer->len_used));
	PyErr_SetObject(YottaDBError, tuple);
}


/* SIMPLE AND SIMPLE THREADED API WRAPPERS */

/* FOR ALL PROXY FUNCTIONS BELOW
 * do almost nothing themselves, simply calls wrapper with a flag for which API they mean to call.
 *
 * Parameters:
 *
 *    self		- the object that this method belongs to (in this case it's the _yottadb module.
 *    args		- a Python tuple of the positional arguments passed to the function
 *    kwds		- a Python dictonary of the keyword arguments passed the tho function
 */

/* FOR ALL BELOW WRAPPERS:
 * does all the work to wrap the 2 related functions using the api_type flag to make the few modifications related how the
 * simple and simple threaded APIs are different.
 *
 * Parameters:
 *    self, args, kwds	- same as proxy functions.
 *    api_type 			- either SIMPLE or SIMPLE_THREADED used by the proxy functions to indicate which API was being called.
 *
 * FOR ALL
 */

/* Wrapper for ydb_data_s and ydb_data_st. */
static PyObject* data(PyObject* self, PyObject* args, PyObject* kwds, api_type api)
{
	bool return_NULL = false;
	char *varname;
	int varname_len, subs_used, status;
	unsigned int *ret_value;
	uint64_t tp_token;
	PyObject *subsarray, *return_python_int;
	ydb_buffer_t *error_string_buffer, *varname_y, *subsarray_y;

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
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	ret_value = (unsigned int*) malloc(sizeof(unsigned int));

	/* Call the wrapped function */
	CALL_WRAP_4(api, ydb_data_s, ydb_data_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, ret_value, status);

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	}

	/* Create Python object to return */
	if (!return_NULL)
		return_python_int = Py_BuildValue("I", *ret_value);

	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);
	free(ret_value);

	if (return_NULL)
		return NULL;
	else
		return return_python_int;
}

/* Proxy for ydb_data_s() */
static PyObject* data_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return data(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_data_st() */
static PyObject* data_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return data(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_delete_s() and ydb_delete_st() */
static PyObject* delete_wrapper(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int deltype, status, varname_len, subs_used;
	char *varname;
	uint64_t tp_token;
	PyObject *subsarray;
	ydb_buffer_t *error_string_buffer,  *varname_y, *subsarray_y;

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

	/* Setup for Call */
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);

	/* Call the wrapped function */
	CALL_WRAP_4(api, ydb_delete_s, ydb_delete_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, deltype, status);


	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);
	if (return_NULL)
		return NULL;
	else
		return Py_None;
}

/* Proxy for ydb_delete_s() */
static PyObject* delete_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return delete_wrapper(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_delete_st() */
static PyObject* delete_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return delete_wrapper(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_delete_excl_s() and ydb_delete_excl_st() */
static PyObject* delete_excel(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int namecount, status;
	uint64_t tp_token;
	PyObject *varnames;
	ydb_buffer_t *error_string_buffer;

	/* Defaults for non-required arguments */
	varnames = Py_None;
	tp_token = YDB_NOTTP;

	/* parse and validate */
	static char* kwlist[] = {"varnames", "tp_token", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OK", kwlist, &varnames, &tp_token))
		return NULL;

	if(varnames != NULL && !validate_sequence_of_bytes(varnames))
	{
		PyErr_SetString(PyExc_TypeError, "'varnames' must be an sequence of bytes.");
		return NULL;
	}

	/* Setup for Call */
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	namecount = 0;
	if (varnames != Py_None)
		namecount = PySequence_Length(varnames);
	ydb_buffer_t* varnames_ydb = convert_py_bytes_sequence_to_ydb_buffer_array(varnames);

	CALL_WRAP_2(api, ydb_delete_excl_s, ydb_delete_excl_st, tp_token, error_string_buffer, namecount, varnames_ydb, status);

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		/* free allocated memory */
		return_NULL = true;
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(error_string_buffer);
	free_buffer_array(varnames_ydb, namecount);
	if (return_NULL)
		return NULL;
	else
		return Py_None;
}

/* Proxy for ydb_delete_excl_s() */
static PyObject* delete_excel_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return delete_excel(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_delete_excl_st() */
static PyObject* delete_excel_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return delete_excel(self, args, kwds, SIMPLE_THREADED);
}



/* Wrapper for ydb_get_s() and ydb_get_st() */
static PyObject* get(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int subs_used, status, return_length, varname_len;
	char *varname;
	uint64_t tp_token;
	PyObject *subsarray, *return_python_string;
	ydb_buffer_t *varname_y, *error_string_buffer, *ret_value, *subsarray_y;

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
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	ret_value = empty_buffer(1024);

	/* Call the wrapped function */
	CALL_WRAP_4(api, ydb_get_s, ydb_get_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, ret_value, status);

	/* check to see if length of string was longer than 1024 is so, try again with proper length */
	if (status == YDB_ERR_INVSTRLEN)
	{
		return_length = ret_value->len_used;
		YDB_FREE_BUFFER(ret_value);
		ret_value = empty_buffer(return_length);
		/* Call the wrapped function */
		CALL_WRAP_4(api, ydb_get_s, ydb_get_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, ret_value, status);
	}

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		/* free allocated memory */
		return_NULL = true;
	}
	/* Create Python object to return */
	if (!return_NULL)
		return_python_string = Py_BuildValue("y#", ret_value->buf_addr, ret_value->len_used);

	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);
	YDB_FREE_BUFFER(ret_value);

	if (return_NULL)
		return NULL;
	else
		return return_python_string;
}

/* Proxy for ydb_get_s() */
static PyObject* get_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return get(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_get_st() */
static PyObject* get_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return get(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_incr_s() and ydb_incr_st() */
static PyObject* incr(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int status, subs_used;
	uint64_t tp_token;
	PyObject *varname, *subsarray, *default_increment, *increment, *return_python_string;
	ydb_buffer_t *increment_ydb, *error_string_buffer, *ret_value, *varname_y, *subsarray_y;

	/* Defaults for non-required arguments */
	subsarray = Py_None;
	tp_token = YDB_NOTTP;
	default_increment = Py_BuildValue("i", 1);
	increment = default_increment;

	/* parse and validate */
	static char* kwlist[] = {"varname", "subsarray", "increment", "tp_token", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOK", kwlist, &varname, &subsarray, &increment, &tp_token)) {
		Py_DECREF(default_increment);
		return NULL;
	}

	if (!PyBytes_Check(increment)) {
		PyErr_Format(PyExc_TypeError, "'varname' must be a bytes object. recieved %s", increment);
		Py_DECREF(default_increment);
		return NULL;
	}
	if (!validate_subsarray_object(subsarray)) {
		Py_DECREF(default_increment);
		return NULL;
	}

	if (!PyBytes_Check(increment)) {
		PyErr_Format(PyExc_TypeError, "'increment' must be a bytes object. recieved %s", increment);
		Py_DECREF(default_increment);
		return NULL;
	}
	/* Setup for Call */
	varname_y = convert_py_bytes_to_ydb_buffer_t(varname);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);

	increment_ydb = convert_py_bytes_to_ydb_buffer_t(increment);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	ret_value = empty_buffer(50);

	/* Call the wrapped function */
	CALL_WRAP_5(api, ydb_incr_s, ydb_incr_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, increment_ydb, ret_value, status);

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		/* free allocated memory */
		return_NULL = true;
	}

	/* Create Python object to return */
	if (!return_NULL)
		return_python_string = Py_BuildValue("y#", ret_value->buf_addr, ret_value->len_used);

	/* free allocated memory */
	Py_DECREF(default_increment);
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(increment_ydb);
	YDB_FREE_BUFFER(error_string_buffer);
	YDB_FREE_BUFFER(ret_value);

	if (return_NULL)
		return NULL;
	else
		return return_python_string;
}

/* Proxy for ydb_incr_s() */
static PyObject* incr_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return incr(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_incr_st() */
static PyObject* incr_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return incr(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_lock_s() and ydb_lock_st() */
static PyObject* lock(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int keys_len, initial_arguments, number_of_arguments;
	uint64_t tp_token;
	unsigned long long timeout_nsec;
	ffi_cif call_interface;
	ffi_type *ret_type;
	PyObject *keys, *keys_default;
	ydb_buffer_t *error_string_buffer;
	key_ydb *keys_ydb;

	/* Defaults for non-required arguments */
	timeout_nsec = 0;
	tp_token = YDB_NOTTP;
	keys_default = PyTuple_New(0);
	keys = keys_default;

	/* parse and validate */
	static char* kwlist[] = {"keys", "timeout_nsec", "tp_token", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OKK", kwlist, &keys, &timeout_nsec, &tp_token))
		return NULL;

	if (!validate_py_keys_sequence_bytes(keys))
		return NULL;
	keys_len = PySequence_Length(keys);

	/* Setup for Call */
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	keys_ydb = convert_key_sequence_to_key_ydb_array(keys);
	Py_DECREF(keys_default);

	/* build ffi call */
	ret_type = &ffi_type_sint;
	if (api == SIMPLE_THREADED)
		initial_arguments = 4;
	else if (api == SIMPLE)
		initial_arguments = 2;

	number_of_arguments = initial_arguments + (keys_len * 3);
	ffi_type *arg_types[number_of_arguments];
	void *arg_values[number_of_arguments];
	/* ffi signature */
	if (api == SIMPLE_THREADED)
	{
		arg_types[0] = &ffi_type_uint64; // tptoken
		arg_values[0] = &tp_token; // tptoken
		arg_types[1] = &ffi_type_pointer;// errstr
		arg_values[1] = &error_string_buffer; // errstr
		arg_types[2] = &ffi_type_uint64; // timout_nsec
		arg_values[2] = &timeout_nsec; // timout_nsec
		arg_types[3] = &ffi_type_sint; // namecount
		arg_values[3] = &keys_len; // namecount
	} else if (api == SIMPLE)
	{
		arg_types[0] = &ffi_type_uint64; // timout_nsec
		arg_values[0] = &timeout_nsec; // timout_nsec
		arg_types[1] = &ffi_type_sint; // namecount
		arg_values[1] = &keys_len; // namecount
	}

	for (int i = 0; i < keys_len; i++)
	{
		int first = initial_arguments + 3*i;
		arg_types[first] = &ffi_type_pointer;// varname
		arg_values[first] = &keys_ydb[i].varname; // varname
		arg_types[first + 1] = &ffi_type_sint; // subs_used
		arg_values[first + 1] = &keys_ydb[i].subs_used; // subs_used
		arg_types[first + 2] = &ffi_type_pointer;// subsarray
		arg_values[first + 2] = &keys_ydb[i].subsarray;// subsarray
	}

	int status; // return value
	if (ffi_prep_cif(&call_interface, FFI_DEFAULT_ABI, number_of_arguments, ret_type, arg_types) == FFI_OK)
	{
		/* Call the wrapped function */
		if (api == SIMPLE_THREADED)
			ffi_call(&call_interface, FFI_FN(ydb_lock_st), &status, arg_values);
		else if (api == SIMPLE)
			ffi_call(&call_interface, FFI_FN(ydb_lock_s), &status, arg_values);
	} else
	{
		PyErr_SetString(PyExc_SystemError, "ffi_prep_cif failed ");
		return_NULL = true;

	}

	/* check for errors */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
		return NULL;
	} else if (status==YDB_LOCK_TIMEOUT)
	{
		PyErr_SetString(YottaDBLockTimeout, "Not able to acquire all requested locks in the specified time.");
		return_NULL = true;
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(error_string_buffer);
	free_key_ydb_array(keys_ydb, keys_len);

	if (return_NULL)
		return NULL;
	else
		return Py_None;
}

/* Proxy for ydb_lock_s() */
static PyObject* lock_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return lock(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_lock_st() */
static PyObject* lock_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return lock(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_lock_decr_s() and ydb_lock_decr_st() */
static PyObject* lock_decr(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int status, varname_len, subs_used;
	char *varname;
	uint64_t tp_token;
	PyObject *subsarray;
	ydb_buffer_t* error_string_buffer, *varname_y, *subsarray_y;

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
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);

	/* Call the wrapped function */
	CALL_WRAP_3(api, ydb_lock_decr_s, ydb_lock_decr_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, status);

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);

	if (return_NULL)
		return NULL;
	else
		return Py_None;
}

/* Proxy for ydb_lock_decr_s() */
static PyObject* lock_decr_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return lock_decr(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_lock_decr_st() */
static PyObject* lock_decr_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return lock_decr(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_lock_incr_s() and ydb_lock_incr_st() */
static PyObject* lock_incr(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int status, varname_len, subs_used;
	char *varname;
	uint64_t tp_token;
	unsigned long long timeout_nsec;
	PyObject *subsarray;
	ydb_buffer_t *error_string_buffer, *varname_y, *subsarray_y;

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
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	/* Call the wrapped function */
	CALL_WRAP_4(api, ydb_lock_incr_s, ydb_lock_incr_st, tp_token, error_string_buffer, timeout_nsec, varname_y, subs_used, subsarray_y, status);

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	} else if (status==YDB_LOCK_TIMEOUT)
	{
		PyErr_SetString(YottaDBLockTimeout, "Not able to acquire all requested locks in the specified time.");
		return_NULL = true;
	}

	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);

	if (return_NULL)
		return NULL;
	else
		return Py_None;
}

/* Proxy for ydb_lock_incr_s() */
static PyObject* lock_incr_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return lock_incr(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_lock_incr_st() */
static PyObject* lock_incr_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return lock_incr(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_node_next_s() and ydb_node_next_st() */
static PyObject* node_next(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int max_subscript_string, default_ret_subs_used, real_ret_subs_used, ret_subs_used, status, varname_len, subs_used;
	char *varname;
	uint64_t tp_token;
	PyObject *subsarray, *return_tuple;
	ydb_buffer_t *error_string_buffer, *ret_subsarray, *varname_y, *subsarray_y;

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
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	max_subscript_string = 1024;
	default_ret_subs_used = subs_used + 5;
	if (default_ret_subs_used > YDB_MAX_SUBS)
		default_ret_subs_used = YDB_MAX_SUBS;
	real_ret_subs_used = default_ret_subs_used;
	ret_subs_used = default_ret_subs_used;
	ret_subsarray = empty_buffer_array(ret_subs_used, max_subscript_string);

	/* Call the wrapped function */
	CALL_WRAP_5(api, ydb_node_next_s, ydb_node_next_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, &ret_subs_used, ret_subsarray, status);

	/* If not enough buffers in ret_subsarray */
	if (status == YDB_ERR_INSUFFSUBS)
	{
		free_buffer_array(ret_subsarray, default_ret_subs_used);
		real_ret_subs_used = ret_subs_used;
		ret_subsarray = empty_buffer_array(real_ret_subs_used, max_subscript_string);
		/* recall the wrapped function */
		CALL_WRAP_5(api, ydb_node_next_s, ydb_node_next_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, &ret_subs_used, ret_subsarray, status);
	}

	/* if a buffer is not long enough */
	while(status == YDB_ERR_INVSTRLEN)
	{
		max_subscript_string = ret_subsarray[ret_subs_used].len_used;
		free(ret_subsarray[ret_subs_used].buf_addr);
		ret_subsarray[ret_subs_used].buf_addr = (char*)malloc(ret_subsarray[ret_subs_used].len_used*sizeof(char));
		ret_subsarray[ret_subs_used].len_alloc = ret_subsarray[ret_subs_used].len_used;
		ret_subsarray[ret_subs_used].len_used = 0;
		ret_subs_used = real_ret_subs_used;
		/* recall the wrapped function */
		CALL_WRAP_5(api, ydb_node_next_s, ydb_node_next_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, &ret_subs_used, ret_subsarray, status);
	}

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	}
	/* Create Python object to return */
	return_tuple = convert_ydb_buffer_array_to_py_tuple(ret_subsarray, ret_subs_used);

	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);
	free_buffer_array(ret_subsarray, real_ret_subs_used);

	if (return_NULL)
		return NULL;
	else
		return return_tuple;
}

/* Proxy for ydb_node_next_s() */
static PyObject* node_next_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return node_next(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_node_next_st() */
static PyObject* node_next_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return node_next(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_node_previous_s() and ydb_node_previous_st() */
static PyObject* node_previous(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int max_subscript_string, default_ret_subs_used, real_ret_subs_used, ret_subs_used, status, varname_len, subs_used;
	char *varname;
	uint64_t tp_token;
	PyObject *subsarray, *return_tuple;
	ydb_buffer_t *error_string_buffer, *ret_subsarray, *varname_y, *subsarray_y;


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
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);

	max_subscript_string = 1024;
	default_ret_subs_used = subs_used - 1;
	if (default_ret_subs_used <= 0)
		default_ret_subs_used = 1;
	real_ret_subs_used = default_ret_subs_used;
	ret_subs_used = default_ret_subs_used;
	ret_subsarray = empty_buffer_array(ret_subs_used, max_subscript_string);

	/* Call the wrapped function */
	CALL_WRAP_5(api, ydb_node_previous_s, ydb_node_previous_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, &ret_subs_used, ret_subsarray, status);

	/* if a buffer is not long enough */
	while(status == YDB_ERR_INVSTRLEN)
	{
		max_subscript_string = ret_subsarray[ret_subs_used].len_used;
		free(ret_subsarray[ret_subs_used].buf_addr);
		ret_subsarray[ret_subs_used].buf_addr = (char*) malloc(ret_subsarray[ret_subs_used].len_used*sizeof(char));
		ret_subsarray[ret_subs_used].len_alloc = ret_subsarray[ret_subs_used].len_used;
		ret_subsarray[ret_subs_used].len_used = 0;
		ret_subs_used = real_ret_subs_used;
		/* recall the wrapped function */
		CALL_WRAP_5(api, ydb_node_previous_s, ydb_node_previous_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, &ret_subs_used, ret_subsarray, status);
	}
	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	}

	/* Create Python object to return */
	if (!return_NULL)
		return_tuple = convert_ydb_buffer_array_to_py_tuple(ret_subsarray, ret_subs_used);

	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);
	free_buffer_array(ret_subsarray, real_ret_subs_used);

	if (return_NULL)
		return NULL;
	else
		return return_tuple;
}

/* Proxy for ydb_node_previous_s() */
static PyObject* node_previous_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return node_previous(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_node_previous_st() */
static PyObject* node_previous_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return node_previous(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_set_s() and ydb_set_st() */
static PyObject* set(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int status, varname_len, value_len, subs_used;
	uint64_t tp_token;
	char *varname, *value;
	PyObject *subsarray;
	ydb_buffer_t *error_string_buffer, *value_buffer, *varname_y, *subsarray_y;


	/* Defaults for non-required arguments */
	subsarray = Py_None;
	tp_token = YDB_NOTTP;
	value = "";

	/* parse and validate */
	static char* kwlist[] = {"varname", "subsarray", "value", "tp_token", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|Oy#K", kwlist, &varname, &varname_len, &subsarray, &value, &value_len, &tp_token))
		return NULL;

	if (!validate_subsarray_object(subsarray))
		return NULL;

	/* Setup for Call */
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	value_buffer = convert_str_to_buffer(value, value_len);

	/* Call the wrapped function */
	CALL_WRAP_4(api, ydb_set_s, ydb_set_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, value_buffer, status);

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	}
	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);

	if (return_NULL)
		return NULL;
	else
		return Py_None;
}

/* Proxy for ydb_set_s() */
static PyObject* set_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return set(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_set_st() */
static PyObject* set_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return set(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_str2zwr_s() and ydb_str2zwr_st() */
static PyObject* str2zwr(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int str_len, status, return_length;
	uint64_t tp_token;
	char *str;
	ydb_buffer_t *error_string_buf, str_buf, *zwr_buf;

	/* Defaults for non-required arguments */
	str = "";
	str_len = 0;
	tp_token = YDB_NOTTP;

	/* parse and validate */
	static char* kwlist[] = {"input", "tp_token", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|K", kwlist, &str, &str_len, &tp_token))
		return NULL;

	/* Setup for Call */
	error_string_buf = empty_buffer(YDB_MAX_ERRORMSG);
	str_buf = (ydb_buffer_t){str_len, str_len, str};
	zwr_buf = empty_buffer(1024);

	/* Call the wrapped function */
	CALL_WRAP_2(api, ydb_str2zwr_s, ydb_str2zwr_st, tp_token, error_string_buf, &str_buf, zwr_buf, status);


	/* recall with properly sized buffer if zwr_buf is not long enough */
	if (status == YDB_ERR_INVSTRLEN)
	{
		return_length = zwr_buf->len_used;
		YDB_FREE_BUFFER(zwr_buf);
		zwr_buf = empty_buffer(return_length);
		/* recall the wrapped function */
		CALL_WRAP_2(api, ydb_str2zwr_s, ydb_str2zwr_st, tp_token, error_string_buf, &str_buf, zwr_buf, status);
	}

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buf);
		return_NULL = true;
	}

	/* Create Python object to return */
	PyObject* return_value =  Py_BuildValue("y#", zwr_buf->buf_addr, zwr_buf->len_used);

	/* free allocated memory */
	YDB_FREE_BUFFER(error_string_buf);
	YDB_FREE_BUFFER(zwr_buf);

	if (return_NULL)
		return NULL;
	else
		return return_value;
}

/* Proxy for ydb_str2zwr_s() */
static PyObject* str2zwr_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return str2zwr(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_str2zwr_st() */
static PyObject* str2zwr_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return str2zwr(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_subscript_next_s() and ydb_subscript_next_st() */
static PyObject* subscript_next(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int status, return_length, varname_len, subs_used;
	char *varname;
	uint64_t tp_token;
	PyObject *subsarray, *return_python_string;
	ydb_buffer_t *error_string_buffer, *ret_value, *varname_y, *subsarray_y;

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
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	ret_value = empty_buffer(1024);

	/* Call the wrapped function */
	CALL_WRAP_4(api, ydb_subscript_next_s, ydb_subscript_next_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, ret_value, status);

	/* check to see if length of string was longer than 1024 is so, try again with proper length */
	if (status == YDB_ERR_INVSTRLEN)
	{
		return_length = ret_value->len_used;
		YDB_FREE_BUFFER(ret_value);
		ret_value = empty_buffer(return_length);
		/* recall the wrapped function */
		CALL_WRAP_4(api, ydb_subscript_next_s, ydb_subscript_next_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, ret_value, status);
	}
	/* check status for Errors and Raise Exception */
	if (status<0) {
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	}

	/* Create Python object to return */
	if (!return_NULL)
		return_python_string = Py_BuildValue("y#", ret_value->buf_addr, ret_value->len_used);

	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);
	YDB_FREE_BUFFER(ret_value);

	if (return_NULL)
		return NULL;
	else
		return return_python_string;
}

/* Proxy for ydb_subscript_next_s() */
static PyObject* subscript_next_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return subscript_next(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_subscript_next_st() */
static PyObject* subscript_next_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return subscript_next(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_subscript_previous_s() and ydb_subscript_previous_st() */
static PyObject* subscript_previous(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int status, return_length, varname_len, subs_used;
	char *varname;
	uint64_t tp_token;
	PyObject *subsarray, *return_python_string;
	ydb_buffer_t *error_string_buffer, *ret_value, *varname_y, *subsarray_y;

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
	varname_y = convert_str_to_buffer(varname, varname_len);
	SETUP_SUBS(subsarray, subs_used, subsarray_y);
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	ret_value = empty_buffer(1024);

	/* Call the wrapped function */
	CALL_WRAP_4(api, ydb_subscript_previous_s, ydb_subscript_previous_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, ret_value, status);

	/* check to see if length of string was longer than 1024 is so, try again with proper length */
	if (status == YDB_ERR_INVSTRLEN)
	{
		return_length = ret_value->len_used;
		YDB_FREE_BUFFER(ret_value);
		ret_value = empty_buffer(return_length);
		CALL_WRAP_4(api, ydb_subscript_previous_s, ydb_subscript_previous_st, tp_token, error_string_buffer, varname_y, subs_used, subsarray_y, ret_value, status);
	}

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	}

	/* Create Python object to return */
	if (!return_NULL)
		return_python_string = Py_BuildValue("y#", ret_value->buf_addr, ret_value->len_used);

	/* free allocated memory */
	YDB_FREE_BUFFER(varname_y);
	free_buffer_array(subsarray_y, subs_used);
	YDB_FREE_BUFFER(error_string_buffer);
	YDB_FREE_BUFFER(ret_value);
	if (return_NULL)
		return NULL;
	else
		return return_python_string;
}

/* Proxy for ydb_subscript_previous_s() */
static PyObject* subscript_previous_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return subscript_previous(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_subscript_previous_st() */
static PyObject* subscript_previous_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return subscript_previous(self, args, kwds, SIMPLE_THREADED);
}

/* Callback functions used by Wrapper for ydb_tp_s() / ydb_tp_st() */

/* Callback Wrapper used by tp_st. The aproach of calling a Python function is a bit of a hack. Here's how it works:
 *    1) This is the callback function always the function passed to called by ydb_tp_st.
 *    2) the actual Python function to be called is passed to this function as the first element in a Python tuple.
 *    3) the positional arguments are passed as the second element and the keyword args are passed as the third.
 *    4) the new tp_token that ydb_tp_st passes to this function as an argument is added to the kwargs dictionary.
 *    5) this function calls calls the python callback funciton with the args and kwargs arguments.
 *    6) if a function raises an exception then this function returns -2 as a way of indicating an error.
 *			(note) the PyErr String is already set so the the function receiving the return value (tp) just needs to return NULL.
 */
static int callback_wrapper_st(uint64_t tp_token, ydb_buffer_t*errstr, void *function_with_arguments)
{ /* this should only ever be called by ydb_tp_st c api via tp below.
   * It assumes that everything passed to it was validated.
   */
	int return_val;
	PyObject *function, *args, *kwargs, *return_value, *tp_token_py;

	function = PyTuple_GetItem(function_with_arguments, 0);
	args = PyTuple_GetItem(function_with_arguments, 1);
	kwargs = PyTuple_GetItem(function_with_arguments, 2);
	tp_token_py = Py_BuildValue("K", tp_token);
	PyDict_SetItemString(kwargs, "tp_token", tp_token_py);
	Py_DECREF(tp_token_py);

	return_value = PyObject_Call(function, args, kwargs);
	if (return_value == NULL) {
		/* function raised an exception */
		return -2; /* MAGIC NUMBER flag to raise exception at the next level up */
	}
	return_val = (int)PyLong_AsLong(return_value);
	Py_DECREF(return_value);
	return return_val;
}
/* Callback Wrapper used by tp_st. The aproach of calling a Python function is a bit of a hack. See notes in 'callback_wrapper_st above */
static int callback_wrapper_s(void *function_with_arguments)
{ /* this should only ever be called by ydb_tp_s c api via tp below.
   * It assumes that everything passed to it was validated.
   */
	int return_val;
	PyObject *function, *args, *kwargs, *return_value;

	function = PyTuple_GetItem(function_with_arguments, 0);
	args = PyTuple_GetItem(function_with_arguments, 1);
	kwargs = PyTuple_GetItem(function_with_arguments, 2);
	return_value = PyObject_Call(function, args, kwargs);
	if (return_value == NULL)
	{	/* function raised an exception */
		return -2; /* MAGIC NUMBER flag to raise exception at the next level up */
	}
	return_val = (int)PyLong_AsLong(return_value);
	Py_DECREF(return_value);
	return return_val;
}

/* Wrapper for ydb_tp_s() / ydb_tp_st() */
static PyObject* tp(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int namecount, status;
	uint64_t tp_token;
	char *transid;
	PyObject *callback, *callback_args, *callback_kwargs, *varnames, *default_varnames_item,*function_with_arguments;
	ydb_buffer_t *error_string_buffer, *varname_buffers;

	/* Defaults for non-required arguments */
	callback_args = PyTuple_New(0);
	callback_kwargs = PyDict_New();
	transid = "BATCH";
	namecount = 1;
	varnames = PyList_New(1); /* place holder. TODO: expose to python call */
	default_varnames_item = Py_BuildValue("y", "*");
	PyList_SetItem(varnames, 0, default_varnames_item); /* default set to special case when all local variables are restored on a restart. */

	tp_token = YDB_NOTTP;

	/* parse and validate */
	static char *kwlist[] = {"callback", "args", "kwargs", "transid", "tp_token", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOsK", kwlist, &callback, &callback_args, &callback_kwargs, &transid, &tp_token))
		return NULL; // raise exception

	/* validate input */
	if (!PyCallable_Check(callback))
	{
		PyErr_SetString(PyExc_TypeError, "'callback' must be a callable.");
		return NULL;
	}
	if (!PyTuple_Check(callback_args))
	{
		PyErr_SetString(PyExc_TypeError, "'args' must be a tuple. (It will be passed to the callback function as positional arguments.)");
		return NULL;
	}
	if (!PyDict_Check(callback_kwargs))
	{
		PyErr_SetString(PyExc_TypeError, "'kwargs' must be a dictionary. (It will be passed to the callback function as keyword arguments.)");
		return NULL;
	}
	/* Setup for Call */
	error_string_buffer = empty_buffer(YDB_MAX_ERRORMSG);
	function_with_arguments = Py_BuildValue("(OOO)", callback, callback_args, callback_kwargs);
	namecount = PySequence_Length(varnames);
	varname_buffers = convert_py_bytes_sequence_to_ydb_buffer_array(varnames);
	Py_DECREF(varnames);

	/* Call the wrapped function */
	if (api == SIMPLE_THREADED)
		status = ydb_tp_st(tp_token, error_string_buffer, callback_wrapper_st, function_with_arguments, transid, namecount, varname_buffers);
	else if (api == SIMPLE)
		status = ydb_tp_s(callback_wrapper_s, function_with_arguments, transid, namecount, varname_buffers);

	/* check status for Errors and Raise Exception */
	if (status==-2) {/* MAGIC VALUE to indicate that the callback
					 * function raised an exception and should be raised
					 */
		return_NULL = true;
	} else if (status<0)
	{
		raise_YottaDBError(status, error_string_buffer);
		return_NULL = true;
	}
	/* free allocated memory */
	YDB_FREE_BUFFER(error_string_buffer);
	free(varname_buffers);
	if (return_NULL)
		return NULL;
	else
		return Py_BuildValue("i", status);
}

/* Proxy for ydb_tp_s() */
static PyObject* tp_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return tp(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_tp_st() */
static PyObject* tp_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return tp(self, args, kwds, SIMPLE_THREADED);
}

/* Wrapper for ydb_zwr2str_s() and ydb_zwr2str_st() */
static PyObject* zwr2str(PyObject* self, PyObject* args, PyObject *kwds, api_type api)
{
	bool return_NULL = false;
	int zwr_len, status, return_length;
	uint64_t tp_token;
	char *zwr;
	PyObject *return_value;
	ydb_buffer_t *error_string_buf, zwr_buf, *str_buf;

	/* Defaults for non-required arguments */
	zwr = "";
	zwr_len = 0;
	tp_token = YDB_NOTTP;

	/* parse and validate */
	static char* kwlist[] = {"input", "tp_token", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y#|K", kwlist, &zwr, &zwr_len, &tp_token))
		return NULL;

	/* Setup for Call */
	error_string_buf = empty_buffer(YDB_MAX_ERRORMSG);
	zwr_buf = (ydb_buffer_t){zwr_len, zwr_len, zwr};
	str_buf = empty_buffer(1024);

	/* Call the wrapped function */
	CALL_WRAP_2(api, ydb_zwr2str_s, ydb_zwr2str_st, tp_token, error_string_buf, &zwr_buf, str_buf, status);

	/* recall with properly sized buffer if zwr_buf is not long enough */
	if (status == YDB_ERR_INVSTRLEN)
	{
		return_length = str_buf->len_used;
		YDB_FREE_BUFFER(str_buf);
		str_buf = empty_buffer(return_length);
		/* recall the wrapped function */
		CALL_WRAP_2(api, ydb_zwr2str_s, ydb_zwr2str_st, tp_token, error_string_buf, &zwr_buf, str_buf, status);
	}

	/* check status for Errors and Raise Exception */
	if (status<0)
	{
		raise_YottaDBError(status, error_string_buf);
		return_NULL = true;
	}

	if (! return_NULL)
		return_value =  Py_BuildValue("y#", str_buf->buf_addr, str_buf->len_used);

	YDB_FREE_BUFFER(error_string_buf);
	YDB_FREE_BUFFER(str_buf);

	if (return_NULL)
		return NULL;
	else
		return return_value;
}

/* Proxy for ydb_zwr2str_s() */
static PyObject* zwr2str_s(PyObject* self, PyObject* args, PyObject* kwds)
{
	return zwr2str(self, args, kwds, SIMPLE);
}

/* Proxy for ydb_zwr2str_st() */
static PyObject* zwr2str_st(PyObject* self, PyObject* args, PyObject* kwds)
{
	return zwr2str(self, args, kwds, SIMPLE_THREADED);
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
	{"data_st", (PyCFunction)data_st, METH_VARARGS | METH_KEYWORDS, "used to learn what type of data is at a node.\n "
																		"0 — There is neither a value nor a subtree, i.e., it is undefined.\n"
																		"1 — There is a value, but no subtree\n"
																		"10 — There is no value, but there is a subtree.\n"
																		"11 — There are both a value and a subtree.\n"},
	{"data_s", (PyCFunction)data_s, METH_VARARGS | METH_KEYWORDS, "used to learn what type of data is at a node.\n "
																		"0 — There is neither a value nor a subtree, i.e., it is undefined.\n"
																		"1 — There is a value, but no subtree\n"
																		"10 — There is no value, but there is a subtree.\n"
																		"11 — There are both a value and a subtree.\n"},
	{"delete_s", (PyCFunction)delete_s, METH_VARARGS | METH_KEYWORDS, "deletes node value or tree data at node"},
	{"delete_st", (PyCFunction)delete_st, METH_VARARGS | METH_KEYWORDS, "deletes node value or tree data at node"},
	{"delete_excel_s", (PyCFunction)delete_excel_s, METH_VARARGS | METH_KEYWORDS, "delete the trees of all local variables except those in the 'varnames' array"},
	{"delete_excel_st", (PyCFunction)delete_excel_st, METH_VARARGS | METH_KEYWORDS, "delete the trees of all local variables except those in the 'varnames' array"},
	{"get_s", (PyCFunction)get_s, METH_VARARGS | METH_KEYWORDS, "returns the value of a node or raises exception"},
	{"get_st", (PyCFunction)get_st, METH_VARARGS | METH_KEYWORDS, "returns the value of a node or raises exception"},
	{"incr_s", (PyCFunction)incr_s, METH_VARARGS | METH_KEYWORDS, "increments value by the value specified by 'increment'"},
	{"incr_st", (PyCFunction)incr_st, METH_VARARGS | METH_KEYWORDS, "increments value by the value specified by 'increment'"},
	{"lock_s", (PyCFunction)lock_s, METH_VARARGS | METH_KEYWORDS, "..."},
	{"lock_st", (PyCFunction)lock_st, METH_VARARGS | METH_KEYWORDS, "..."},
	{"lock_decr_s", (PyCFunction)lock_decr_s, METH_VARARGS | METH_KEYWORDS, "Decrements the count of the specified lock held by the process. As noted in the Concepts section, a lock whose count goes from 1 to 0 is released. A lock whose name is specified, but which the process does not hold, is ignored."},
	{"lock_decr_st", (PyCFunction)lock_decr_st, METH_VARARGS | METH_KEYWORDS, "Decrements the count of the specified lock held by the process. As noted in the Concepts section, a lock whose count goes from 1 to 0 is released. A lock whose name is specified, but which the process does not hold, is ignored."},
	{"lock_incr_s", (PyCFunction)lock_incr_s, METH_VARARGS | METH_KEYWORDS, "Without releasing any locks held by the process, attempt to acquire the requested lock incrementing it if already held."},
	{"lock_incr_st", (PyCFunction)lock_incr_st, METH_VARARGS | METH_KEYWORDS, "Without releasing any locks held by the process, attempt to acquire the requested lock incrementing it if already held."},
	{"str2zwr_s", (PyCFunction)str2zwr_s, METH_VARARGS | METH_KEYWORDS, "returns the zwrite formatted (Bytes Object) version of the Bytes object provided as input."},
	{"str2zwr_st", (PyCFunction)str2zwr_st, METH_VARARGS | METH_KEYWORDS, "returns the zwrite formatted (Bytes Object) version of the Bytes object provided as input."},
	{"node_next_s", (PyCFunction)node_next_s, METH_VARARGS | METH_KEYWORDS, "facilitate depth-first traversal of a local or global variable tree. returns string tuple of subscripts of next node with value."},
	{"node_next_st", (PyCFunction)node_next_st, METH_VARARGS | METH_KEYWORDS, "facilitate depth-first traversal of a local or global variable tree. returns string tuple of subscripts of next node with value."},
	{"node_previous_s", (PyCFunction)node_previous_s, METH_VARARGS | METH_KEYWORDS, "facilitate depth-first traversal of a local or global variable tree. returns string tuple of subscripts of previous node with value."},
	{"node_previous_st", (PyCFunction)node_previous_st, METH_VARARGS | METH_KEYWORDS, "facilitate depth-first traversal of a local or global variable tree. returns string tuple of subscripts of previous node with value."},
	{"set_s", (PyCFunction)set_s, METH_VARARGS | METH_KEYWORDS, "sets the value of a node or raises exception"},
	{"set_st", (PyCFunction)set_st, METH_VARARGS | METH_KEYWORDS, "sets the value of a node or raises exception"},
	{"subscript_next_s", (PyCFunction)subscript_next_s, METH_VARARGS | METH_KEYWORDS, "returns the name of the next subscript at the same level as the one given"},
	{"subscript_next_st", (PyCFunction)subscript_next_st, METH_VARARGS | METH_KEYWORDS, "returns the name of the next subscript at the same level as the one given"},
	{"subscript_previous_s", (PyCFunction)subscript_previous_s, METH_VARARGS | METH_KEYWORDS, "returns the name of the previous subscript at the same level as the one given"},
	{"subscript_previous_st", (PyCFunction)subscript_previous_st, METH_VARARGS | METH_KEYWORDS, "returns the name of the previous subscript at the same level as the one given"},
	{"tp_s", (PyCFunction)tp_s, METH_VARARGS | METH_KEYWORDS, "transaction"},
	{"tp_st", (PyCFunction)tp_st, METH_VARARGS | METH_KEYWORDS, "transaction"},
	{"zwr2str_s", (PyCFunction)zwr2str_s, METH_VARARGS | METH_KEYWORDS, "returns the Bytes Object from the zwrite formated Bytes object provided as input."},
	{"zwr2str_st", (PyCFunction)zwr2str_st, METH_VARARGS | METH_KEYWORDS, "returns the Bytes Object from the zwrite formated Bytes object provided as input."},
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

PyMODINIT_FUNC PyInit__yottadb(void)
{
	PyObject *module = PyModule_Create(&_yottadbmodule);
	if (module == NULL)
		return NULL;

    /* Defining Module 'Constants' */
	PyObject *module_dictionary = PyModule_GetDict(module);
	/* constants defined here for conveniance */
	PyDict_SetItemString(module_dictionary, "YDB_DATA_NO_DATA", Py_BuildValue("i", 0));
	PyDict_SetItemString(module_dictionary, "YDB_DATA_HAS_VALUE_NO_TREE", Py_BuildValue("i", 1));
	PyDict_SetItemString(module_dictionary, "YDB_DATA_NO_VALUE_HAS_TREE", Py_BuildValue("i", 10));
	PyDict_SetItemString(module_dictionary, "YDB_DATA_HAS_VALUE_HAS_TREE", Py_BuildValue("i", 11));
	/* expose constants defined in c */
	PyDict_SetItemString(module_dictionary, "YDB_DEL_TREE", Py_BuildValue("i", YDB_DEL_TREE));
	PyDict_SetItemString(module_dictionary, "YDB_DEL_NODE", Py_BuildValue("i", YDB_DEL_NODE));
	PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_WARNING", Py_BuildValue("i", YDB_SEVERITY_WARNING));
	PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_SUCCESS", Py_BuildValue("i", YDB_SEVERITY_SUCCESS));
	PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_ERROR", Py_BuildValue("i", YDB_SEVERITY_ERROR));
	PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_INFORMATIONAL", Py_BuildValue("i", YDB_SEVERITY_INFORMATIONAL));
	PyDict_SetItemString(module_dictionary, "YDB_SEVERITY_FATAL", Py_BuildValue("i", YDB_SEVERITY_FATAL));
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

	//Error Codes
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ACK", Py_BuildValue("i", YDB_ERR_ACK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BREAKZST", Py_BuildValue("i", YDB_ERR_BREAKZST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADACCMTHD", Py_BuildValue("i", YDB_ERR_BADACCMTHD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADJPIPARAM", Py_BuildValue("i", YDB_ERR_BADJPIPARAM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADSYIPARAM", Py_BuildValue("i", YDB_ERR_BADSYIPARAM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BITMAPSBAD", Py_BuildValue("i", YDB_ERR_BITMAPSBAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BREAK", Py_BuildValue("i", YDB_ERR_BREAK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BREAKDEA", Py_BuildValue("i", YDB_ERR_BREAKDEA));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BREAKZBA", Py_BuildValue("i", YDB_ERR_BREAKZBA));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STATCNT", Py_BuildValue("i", YDB_ERR_STATCNT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BTFAIL", Py_BuildValue("i", YDB_ERR_BTFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUPRECFLLCK", Py_BuildValue("i", YDB_ERR_MUPRECFLLCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CMD", Py_BuildValue("i", YDB_ERR_CMD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COLON", Py_BuildValue("i", YDB_ERR_COLON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COMMA", Py_BuildValue("i", YDB_ERR_COMMA));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COMMAORRPAREXP", Py_BuildValue("i", YDB_ERR_COMMAORRPAREXP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COMMENT", Py_BuildValue("i", YDB_ERR_COMMENT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CTRAP", Py_BuildValue("i", YDB_ERR_CTRAP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CTRLC", Py_BuildValue("i", YDB_ERR_CTRLC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CTRLY", Py_BuildValue("i", YDB_ERR_CTRLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCCERR", Py_BuildValue("i", YDB_ERR_DBCCERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DUPTOKEN", Py_BuildValue("i", YDB_ERR_DUPTOKEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBJNLNOTMATCH", Py_BuildValue("i", YDB_ERR_DBJNLNOTMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFILERR", Py_BuildValue("i", YDB_ERR_DBFILERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBNOTGDS", Py_BuildValue("i", YDB_ERR_DBNOTGDS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBOPNERR", Py_BuildValue("i", YDB_ERR_DBOPNERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRDERR", Py_BuildValue("i", YDB_ERR_DBRDERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEDUMPNOW", Py_BuildValue("i", YDB_ERR_CCEDUMPNOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVPARINAP", Py_BuildValue("i", YDB_ERR_DEVPARINAP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RECORDSTAT", Py_BuildValue("i", YDB_ERR_RECORDSTAT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTGBL", Py_BuildValue("i", YDB_ERR_NOTGBL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVPARPROT", Py_BuildValue("i", YDB_ERR_DEVPARPROT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PREMATEOF", Py_BuildValue("i", YDB_ERR_PREMATEOF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVINVALID", Py_BuildValue("i", YDB_ERR_GVINVALID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVPARTOOBIG", Py_BuildValue("i", YDB_ERR_DEVPARTOOBIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVPARUNK", Py_BuildValue("i", YDB_ERR_DEVPARUNK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVPARVALREQ", Py_BuildValue("i", YDB_ERR_DEVPARVALREQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVPARMNEG", Py_BuildValue("i", YDB_ERR_DEVPARMNEG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSEBLKRDFAIL", Py_BuildValue("i", YDB_ERR_DSEBLKRDFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSEFAIL", Py_BuildValue("i", YDB_ERR_DSEFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTALLREPLON", Py_BuildValue("i", YDB_ERR_NOTALLREPLON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADLKIPARAM", Py_BuildValue("i", YDB_ERR_BADLKIPARAM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLREADBOF", Py_BuildValue("i", YDB_ERR_JNLREADBOF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DVIKEYBAD", Py_BuildValue("i", YDB_ERR_DVIKEYBAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ENQ", Py_BuildValue("i", YDB_ERR_ENQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EQUAL", Py_BuildValue("i", YDB_ERR_EQUAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ERRORSUMMARY", Py_BuildValue("i", YDB_ERR_ERRORSUMMARY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ERRWEXC", Py_BuildValue("i", YDB_ERR_ERRWEXC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ERRWIOEXC", Py_BuildValue("i", YDB_ERR_ERRWIOEXC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ERRWZBRK", Py_BuildValue("i", YDB_ERR_ERRWZBRK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ERRWZTRAP", Py_BuildValue("i", YDB_ERR_ERRWZTRAP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NUMUNXEOR", Py_BuildValue("i", YDB_ERR_NUMUNXEOR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXPR", Py_BuildValue("i", YDB_ERR_EXPR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STRUNXEOR", Py_BuildValue("i", YDB_ERR_STRUNXEOR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLEXTEND", Py_BuildValue("i", YDB_ERR_JNLEXTEND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FCHARMAXARGS", Py_BuildValue("i", YDB_ERR_FCHARMAXARGS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FCNSVNEXPECTED", Py_BuildValue("i", YDB_ERR_FCNSVNEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FNARGINC", Py_BuildValue("i", YDB_ERR_FNARGINC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLACCESS", Py_BuildValue("i", YDB_ERR_JNLACCESS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRANSNOSTART", Py_BuildValue("i", YDB_ERR_TRANSNOSTART));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FNUMARG", Py_BuildValue("i", YDB_ERR_FNUMARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FOROFLOW", Py_BuildValue("i", YDB_ERR_FOROFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_YDIRTSZ", Py_BuildValue("i", YDB_ERR_YDIRTSZ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSUCCESS", Py_BuildValue("i", YDB_ERR_JNLSUCCESS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GBLNAME", Py_BuildValue("i", YDB_ERR_GBLNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GBLOFLOW", Py_BuildValue("i", YDB_ERR_GBLOFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CORRUPT", Py_BuildValue("i", YDB_ERR_CORRUPT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMCHECK", Py_BuildValue("i", YDB_ERR_GTMCHECK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVDATAFAIL", Py_BuildValue("i", YDB_ERR_GVDATAFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EORNOTFND", Py_BuildValue("i", YDB_ERR_EORNOTFND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVGETFAIL", Py_BuildValue("i", YDB_ERR_GVGETFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVIS", Py_BuildValue("i", YDB_ERR_GVIS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVKILLFAIL", Py_BuildValue("i", YDB_ERR_GVKILLFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVNAKED", Py_BuildValue("i", YDB_ERR_GVNAKED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVNEXTARG", Py_BuildValue("i", YDB_ERR_GVNEXTARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVORDERFAIL", Py_BuildValue("i", YDB_ERR_GVORDERFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVPUTFAIL", Py_BuildValue("i", YDB_ERR_GVPUTFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATTABSYNTAX", Py_BuildValue("i", YDB_ERR_PATTABSYNTAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVSUBOFLOW", Py_BuildValue("i", YDB_ERR_GVSUBOFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVUNDEF", Py_BuildValue("i", YDB_ERR_GVUNDEF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRANSNEST", Py_BuildValue("i", YDB_ERR_TRANSNEST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INDEXTRACHARS", Py_BuildValue("i", YDB_ERR_INDEXTRACHARS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CORRUPTNODE", Py_BuildValue("i", YDB_ERR_CORRUPTNODE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INDRMAXLEN", Py_BuildValue("i", YDB_ERR_INDRMAXLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG268", Py_BuildValue("i", YDB_ERR_UNUSEDMSG268));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INTEGERRS", Py_BuildValue("i", YDB_ERR_INTEGERRS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVCMD", Py_BuildValue("i", YDB_ERR_INVCMD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVFCN", Py_BuildValue("i", YDB_ERR_INVFCN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVOBJ", Py_BuildValue("i", YDB_ERR_INVOBJ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVSVN", Py_BuildValue("i", YDB_ERR_INVSVN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_IOEOF", Py_BuildValue("i", YDB_ERR_IOEOF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_IONOTOPEN", Py_BuildValue("i", YDB_ERR_IONOTOPEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUPIPINFO", Py_BuildValue("i", YDB_ERR_MUPIPINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG277", Py_BuildValue("i", YDB_ERR_UNUSEDMSG277));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBFAIL", Py_BuildValue("i", YDB_ERR_JOBFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBLABOFF", Py_BuildValue("i", YDB_ERR_JOBLABOFF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBPARNOVAL", Py_BuildValue("i", YDB_ERR_JOBPARNOVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBPARNUM", Py_BuildValue("i", YDB_ERR_JOBPARNUM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBPARSTR", Py_BuildValue("i", YDB_ERR_JOBPARSTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBPARUNK", Py_BuildValue("i", YDB_ERR_JOBPARUNK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBPARVALREQ", Py_BuildValue("i", YDB_ERR_JOBPARVALREQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JUSTFRACT", Py_BuildValue("i", YDB_ERR_JUSTFRACT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_KEY2BIG", Py_BuildValue("i", YDB_ERR_KEY2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LABELEXPECTED", Py_BuildValue("i", YDB_ERR_LABELEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LABELMISSING", Py_BuildValue("i", YDB_ERR_LABELMISSING));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LABELUNKNOWN", Py_BuildValue("i", YDB_ERR_LABELUNKNOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DIVZERO", Py_BuildValue("i", YDB_ERR_DIVZERO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LKNAMEXPECTED", Py_BuildValue("i", YDB_ERR_LKNAMEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLRDERR", Py_BuildValue("i", YDB_ERR_JNLRDERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADRUNNING", Py_BuildValue("i", YDB_ERR_LOADRUNNING));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LPARENMISSING", Py_BuildValue("i", YDB_ERR_LPARENMISSING));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LSEXPECTED", Py_BuildValue("i", YDB_ERR_LSEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LVORDERARG", Py_BuildValue("i", YDB_ERR_LVORDERARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXFORARGS", Py_BuildValue("i", YDB_ERR_MAXFORARGS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRANSMINUS", Py_BuildValue("i", YDB_ERR_TRANSMINUS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXNRSUBSCRIPTS", Py_BuildValue("i", YDB_ERR_MAXNRSUBSCRIPTS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXSTRLEN", Py_BuildValue("i", YDB_ERR_MAXSTRLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ENCRYPTCONFLT2", Py_BuildValue("i", YDB_ERR_ENCRYPTCONFLT2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFILOPN", Py_BuildValue("i", YDB_ERR_JNLFILOPN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MBXRDONLY", Py_BuildValue("i", YDB_ERR_MBXRDONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLINVALID", Py_BuildValue("i", YDB_ERR_JNLINVALID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MBXWRTONLY", Py_BuildValue("i", YDB_ERR_MBXWRTONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MEMORY", Py_BuildValue("i", YDB_ERR_MEMORY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG305", Py_BuildValue("i", YDB_ERR_UNUSEDMSG305));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG306", Py_BuildValue("i", YDB_ERR_UNUSEDMSG306));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG307", Py_BuildValue("i", YDB_ERR_UNUSEDMSG307));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG308", Py_BuildValue("i", YDB_ERR_UNUSEDMSG308));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG309", Py_BuildValue("i", YDB_ERR_UNUSEDMSG309));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG310", Py_BuildValue("i", YDB_ERR_UNUSEDMSG310));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG311", Py_BuildValue("i", YDB_ERR_UNUSEDMSG311));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG312", Py_BuildValue("i", YDB_ERR_UNUSEDMSG312));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG313", Py_BuildValue("i", YDB_ERR_UNUSEDMSG313));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG314", Py_BuildValue("i", YDB_ERR_UNUSEDMSG314));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLTMQUAL3", Py_BuildValue("i", YDB_ERR_JNLTMQUAL3));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MULTLAB", Py_BuildValue("i", YDB_ERR_MULTLAB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BLKCNT", Py_BuildValue("i", YDB_ERR_BLKCNT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEDUMPOFF", Py_BuildValue("i", YDB_ERR_CCEDUMPOFF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOPLACE", Py_BuildValue("i", YDB_ERR_NOPLACE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLCLOSE", Py_BuildValue("i", YDB_ERR_JNLCLOSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTPRINCIO", Py_BuildValue("i", YDB_ERR_NOTPRINCIO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTTOEOFONPUT", Py_BuildValue("i", YDB_ERR_NOTTOEOFONPUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOZBRK", Py_BuildValue("i", YDB_ERR_NOZBRK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NULSUBSC", Py_BuildValue("i", YDB_ERR_NULSUBSC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NUMOFLOW", Py_BuildValue("i", YDB_ERR_NUMOFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PARFILSPC", Py_BuildValue("i", YDB_ERR_PARFILSPC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATCLASS", Py_BuildValue("i", YDB_ERR_PATCLASS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATCODE", Py_BuildValue("i", YDB_ERR_PATCODE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATLIT", Py_BuildValue("i", YDB_ERR_PATLIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATMAXLEN", Py_BuildValue("i", YDB_ERR_PATMAXLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LPARENREQD", Py_BuildValue("i", YDB_ERR_LPARENREQD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATUPPERLIM", Py_BuildValue("i", YDB_ERR_PATUPPERLIM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PCONDEXPECTED", Py_BuildValue("i", YDB_ERR_PCONDEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PRCNAMLEN", Py_BuildValue("i", YDB_ERR_PRCNAMLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RANDARGNEG", Py_BuildValue("i", YDB_ERR_RANDARGNEG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBPRIVERR", Py_BuildValue("i", YDB_ERR_DBPRIVERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REC2BIG", Py_BuildValue("i", YDB_ERR_REC2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RHMISSING", Py_BuildValue("i", YDB_ERR_RHMISSING));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVICEREADONLY", Py_BuildValue("i", YDB_ERR_DEVICEREADONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COLLDATAEXISTS", Py_BuildValue("i", YDB_ERR_COLLDATAEXISTS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ROUTINEUNKNOWN", Py_BuildValue("i", YDB_ERR_ROUTINEUNKNOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RPARENMISSING", Py_BuildValue("i", YDB_ERR_RPARENMISSING));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RTNNAME", Py_BuildValue("i", YDB_ERR_RTNNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VIEWGVN", Py_BuildValue("i", YDB_ERR_VIEWGVN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RTSLOC", Py_BuildValue("i", YDB_ERR_RTSLOC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RWARG", Py_BuildValue("i", YDB_ERR_RWARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RWFORMAT", Py_BuildValue("i", YDB_ERR_RWFORMAT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLWRTDEFER", Py_BuildValue("i", YDB_ERR_JNLWRTDEFER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SELECTFALSE", Py_BuildValue("i", YDB_ERR_SELECTFALSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SPOREOL", Py_BuildValue("i", YDB_ERR_SPOREOL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRCLIN", Py_BuildValue("i", YDB_ERR_SRCLIN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRCLOC", Py_BuildValue("i", YDB_ERR_SRCLOC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRCLOCUNKNOWN", Py_BuildValue("i", YDB_ERR_SRCLOCUNKNOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STACKCRIT", Py_BuildValue("i", YDB_ERR_STACKCRIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STACKOFLOW", Py_BuildValue("i", YDB_ERR_STACKOFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STACKUNDERFLO", Py_BuildValue("i", YDB_ERR_STACKUNDERFLO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STRINGOFLOW", Py_BuildValue("i", YDB_ERR_STRINGOFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SVNOSET", Py_BuildValue("i", YDB_ERR_SVNOSET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VIEWFN", Py_BuildValue("i", YDB_ERR_VIEWFN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TERMASTQUOTA", Py_BuildValue("i", YDB_ERR_TERMASTQUOTA));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TEXTARG", Py_BuildValue("i", YDB_ERR_TEXTARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TMPSTOREMAX", Py_BuildValue("i", YDB_ERR_TMPSTOREMAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VIEWCMD", Py_BuildValue("i", YDB_ERR_VIEWCMD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNI", Py_BuildValue("i", YDB_ERR_JNI));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TXTSRCFMT", Py_BuildValue("i", YDB_ERR_TXTSRCFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UIDMSG", Py_BuildValue("i", YDB_ERR_UIDMSG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UIDSND", Py_BuildValue("i", YDB_ERR_UIDSND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LVUNDEF", Py_BuildValue("i", YDB_ERR_LVUNDEF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNIMPLOP", Py_BuildValue("i", YDB_ERR_UNIMPLOP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VAREXPECTED", Py_BuildValue("i", YDB_ERR_VAREXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VARRECBLKSZ", Py_BuildValue("i", YDB_ERR_VARRECBLKSZ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXARGCNT", Py_BuildValue("i", YDB_ERR_MAXARGCNT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSEMGET", Py_BuildValue("i", YDB_ERR_GTMSECSHRSEMGET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VIEWARGCNT", Py_BuildValue("i", YDB_ERR_VIEWARGCNT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRDMNSTARTED", Py_BuildValue("i", YDB_ERR_GTMSECSHRDMNSTARTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZATTACHERR", Py_BuildValue("i", YDB_ERR_ZATTACHERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZDATEFMT", Py_BuildValue("i", YDB_ERR_ZDATEFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZEDFILSPEC", Py_BuildValue("i", YDB_ERR_ZEDFILSPEC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZFILENMTOOLONG", Py_BuildValue("i", YDB_ERR_ZFILENMTOOLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZFILKEYBAD", Py_BuildValue("i", YDB_ERR_ZFILKEYBAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZFILNMBAD", Py_BuildValue("i", YDB_ERR_ZFILNMBAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZGOTOLTZERO", Py_BuildValue("i", YDB_ERR_ZGOTOLTZERO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZGOTOTOOBIG", Py_BuildValue("i", YDB_ERR_ZGOTOTOOBIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZLINKFILE", Py_BuildValue("i", YDB_ERR_ZLINKFILE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZPARSETYPE", Py_BuildValue("i", YDB_ERR_ZPARSETYPE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZPARSFLDBAD", Py_BuildValue("i", YDB_ERR_ZPARSFLDBAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZPIDBADARG", Py_BuildValue("i", YDB_ERR_ZPIDBADARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG390", Py_BuildValue("i", YDB_ERR_UNUSEDMSG390));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG391", Py_BuildValue("i", YDB_ERR_UNUSEDMSG391));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZPRTLABNOTFND", Py_BuildValue("i", YDB_ERR_ZPRTLABNOTFND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VIEWAMBIG", Py_BuildValue("i", YDB_ERR_VIEWAMBIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VIEWNOTFOUND", Py_BuildValue("i", YDB_ERR_VIEWNOTFOUND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG395", Py_BuildValue("i", YDB_ERR_UNUSEDMSG395));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVSPECREC", Py_BuildValue("i", YDB_ERR_INVSPECREC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG397", Py_BuildValue("i", YDB_ERR_UNUSEDMSG397));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZSRCHSTRMCT", Py_BuildValue("i", YDB_ERR_ZSRCHSTRMCT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VERSION", Py_BuildValue("i", YDB_ERR_VERSION));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUNOTALLSEC", Py_BuildValue("i", YDB_ERR_MUNOTALLSEC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUSECDEL", Py_BuildValue("i", YDB_ERR_MUSECDEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUSECNOTDEL", Py_BuildValue("i", YDB_ERR_MUSECNOTDEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RPARENREQD", Py_BuildValue("i", YDB_ERR_RPARENREQD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZGBLDIRACC", Py_BuildValue("i", YDB_ERR_ZGBLDIRACC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVNAKEDEXTNM", Py_BuildValue("i", YDB_ERR_GVNAKEDEXTNM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTGBLDEL", Py_BuildValue("i", YDB_ERR_EXTGBLDEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSEWCINITCON", Py_BuildValue("i", YDB_ERR_DSEWCINITCON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LASTFILCMPLD", Py_BuildValue("i", YDB_ERR_LASTFILCMPLD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOEXCNOZTRAP", Py_BuildValue("i", YDB_ERR_NOEXCNOZTRAP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNSDCLASS", Py_BuildValue("i", YDB_ERR_UNSDCLASS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNSDDTYPE", Py_BuildValue("i", YDB_ERR_UNSDDTYPE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCUNKTYPE", Py_BuildValue("i", YDB_ERR_ZCUNKTYPE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCUNKMECH", Py_BuildValue("i", YDB_ERR_ZCUNKMECH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCUNKQUAL", Py_BuildValue("i", YDB_ERR_ZCUNKQUAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLDBTNNOMATCH", Py_BuildValue("i", YDB_ERR_JNLDBTNNOMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCALLTABLE", Py_BuildValue("i", YDB_ERR_ZCALLTABLE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCARGMSMTCH", Py_BuildValue("i", YDB_ERR_ZCARGMSMTCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCCONMSMTCH", Py_BuildValue("i", YDB_ERR_ZCCONMSMTCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCOPT0", Py_BuildValue("i", YDB_ERR_ZCOPT0));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG420", Py_BuildValue("i", YDB_ERR_UNUSEDMSG420));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG421", Py_BuildValue("i", YDB_ERR_UNUSEDMSG421));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCPOSOVR", Py_BuildValue("i", YDB_ERR_ZCPOSOVR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCINPUTREQ", Py_BuildValue("i", YDB_ERR_ZCINPUTREQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLTNOUTOFSEQ", Py_BuildValue("i", YDB_ERR_JNLTNOUTOFSEQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ACTRANGE", Py_BuildValue("i", YDB_ERR_ACTRANGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCCONVERT", Py_BuildValue("i", YDB_ERR_ZCCONVERT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCRTENOTF", Py_BuildValue("i", YDB_ERR_ZCRTENOTF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVRUNDOWN", Py_BuildValue("i", YDB_ERR_GVRUNDOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LKRUNDOWN", Py_BuildValue("i", YDB_ERR_LKRUNDOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_IORUNDOWN", Py_BuildValue("i", YDB_ERR_IORUNDOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILENOTFND", Py_BuildValue("i", YDB_ERR_FILENOTFND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUFILRNDWNFL", Py_BuildValue("i", YDB_ERR_MUFILRNDWNFL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLTMQUAL1", Py_BuildValue("i", YDB_ERR_JNLTMQUAL1));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FORCEDHALT", Py_BuildValue("i", YDB_ERR_FORCEDHALT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADEOF", Py_BuildValue("i", YDB_ERR_LOADEOF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WILLEXPIRE", Py_BuildValue("i", YDB_ERR_WILLEXPIRE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADEDBG", Py_BuildValue("i", YDB_ERR_LOADEDBG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LABELONLY", Py_BuildValue("i", YDB_ERR_LABELONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUREORGFAIL", Py_BuildValue("i", YDB_ERR_MUREORGFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVZPREVFAIL", Py_BuildValue("i", YDB_ERR_GVZPREVFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MULTFORMPARM", Py_BuildValue("i", YDB_ERR_MULTFORMPARM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_QUITARGUSE", Py_BuildValue("i", YDB_ERR_QUITARGUSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NAMEEXPECTED", Py_BuildValue("i", YDB_ERR_NAMEEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FALLINTOFLST", Py_BuildValue("i", YDB_ERR_FALLINTOFLST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTEXTRINSIC", Py_BuildValue("i", YDB_ERR_NOTEXTRINSIC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRREMSEMFAIL", Py_BuildValue("i", YDB_ERR_GTMSECSHRREMSEMFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FMLLSTMISSING", Py_BuildValue("i", YDB_ERR_FMLLSTMISSING));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ACTLSTTOOLONG", Py_BuildValue("i", YDB_ERR_ACTLSTTOOLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ACTOFFSET", Py_BuildValue("i", YDB_ERR_ACTOFFSET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXACTARG", Py_BuildValue("i", YDB_ERR_MAXACTARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRREMSEM", Py_BuildValue("i", YDB_ERR_GTMSECSHRREMSEM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLTMQUAL2", Py_BuildValue("i", YDB_ERR_JNLTMQUAL2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GDINVALID", Py_BuildValue("i", YDB_ERR_GDINVALID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ASSERT", Py_BuildValue("i", YDB_ERR_ASSERT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUFILRNDWNSUC", Py_BuildValue("i", YDB_ERR_MUFILRNDWNSUC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADEDSZ", Py_BuildValue("i", YDB_ERR_LOADEDSZ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_QUITARGLST", Py_BuildValue("i", YDB_ERR_QUITARGLST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_QUITARGREQD", Py_BuildValue("i", YDB_ERR_QUITARGREQD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRITRESET", Py_BuildValue("i", YDB_ERR_CRITRESET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNKNOWNFOREX", Py_BuildValue("i", YDB_ERR_UNKNOWNFOREX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FSEXP", Py_BuildValue("i", YDB_ERR_FSEXP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WILDCARD", Py_BuildValue("i", YDB_ERR_WILDCARD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DIRONLY", Py_BuildValue("i", YDB_ERR_DIRONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILEPARSE", Py_BuildValue("i", YDB_ERR_FILEPARSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_QUALEXP", Py_BuildValue("i", YDB_ERR_QUALEXP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADQUAL", Py_BuildValue("i", YDB_ERR_BADQUAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_QUALVAL", Py_BuildValue("i", YDB_ERR_QUALVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZROSYNTAX", Py_BuildValue("i", YDB_ERR_ZROSYNTAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COMPILEQUALS", Py_BuildValue("i", YDB_ERR_COMPILEQUALS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZLNOOBJECT", Py_BuildValue("i", YDB_ERR_ZLNOOBJECT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZLMODULE", Py_BuildValue("i", YDB_ERR_ZLMODULE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBLEVMX", Py_BuildValue("i", YDB_ERR_DBBLEVMX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBLEVMN", Py_BuildValue("i", YDB_ERR_DBBLEVMN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBSIZMN", Py_BuildValue("i", YDB_ERR_DBBSIZMN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBSIZMX", Py_BuildValue("i", YDB_ERR_DBBSIZMX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRSIZMN", Py_BuildValue("i", YDB_ERR_DBRSIZMN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRSIZMX", Py_BuildValue("i", YDB_ERR_DBRSIZMX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCMPNZRO", Py_BuildValue("i", YDB_ERR_DBCMPNZRO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBSTARSIZ", Py_BuildValue("i", YDB_ERR_DBSTARSIZ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBSTARCMP", Py_BuildValue("i", YDB_ERR_DBSTARCMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCMPMX", Py_BuildValue("i", YDB_ERR_DBCMPMX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBKEYMX", Py_BuildValue("i", YDB_ERR_DBKEYMX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBKEYMN", Py_BuildValue("i", YDB_ERR_DBKEYMN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCMPBAD", Py_BuildValue("i", YDB_ERR_DBCMPBAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBKEYORD", Py_BuildValue("i", YDB_ERR_DBKEYORD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBPTRNOTPOS", Py_BuildValue("i", YDB_ERR_DBPTRNOTPOS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBPTRMX", Py_BuildValue("i", YDB_ERR_DBPTRMX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBPTRMAP", Py_BuildValue("i", YDB_ERR_DBPTRMAP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_IFBADPARM", Py_BuildValue("i", YDB_ERR_IFBADPARM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_IFNOTINIT", Py_BuildValue("i", YDB_ERR_IFNOTINIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSOCKET", Py_BuildValue("i", YDB_ERR_GTMSECSHRSOCKET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADBGSZ", Py_BuildValue("i", YDB_ERR_LOADBGSZ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADFMT", Py_BuildValue("i", YDB_ERR_LOADFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADFILERR", Py_BuildValue("i", YDB_ERR_LOADFILERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOREGION", Py_BuildValue("i", YDB_ERR_NOREGION));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATLOAD", Py_BuildValue("i", YDB_ERR_PATLOAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTRACTFILERR", Py_BuildValue("i", YDB_ERR_EXTRACTFILERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FREEZE", Py_BuildValue("i", YDB_ERR_FREEZE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOSELECT", Py_BuildValue("i", YDB_ERR_NOSELECT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTRFAIL", Py_BuildValue("i", YDB_ERR_EXTRFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LDBINFMT", Py_BuildValue("i", YDB_ERR_LDBINFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOPREVLINK", Py_BuildValue("i", YDB_ERR_NOPREVLINK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEDUMPON", Py_BuildValue("i", YDB_ERR_CCEDUMPON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEDMPQUALREQ", Py_BuildValue("i", YDB_ERR_CCEDMPQUALREQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEDBDUMP", Py_BuildValue("i", YDB_ERR_CCEDBDUMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEDBNODUMP", Py_BuildValue("i", YDB_ERR_CCEDBNODUMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCPMBX", Py_BuildValue("i", YDB_ERR_CCPMBX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REQRUNDOWN", Py_BuildValue("i", YDB_ERR_REQRUNDOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCPINTQUE", Py_BuildValue("i", YDB_ERR_CCPINTQUE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCPBADMSG", Py_BuildValue("i", YDB_ERR_CCPBADMSG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CNOTONSYS", Py_BuildValue("i", YDB_ERR_CNOTONSYS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCPNAME", Py_BuildValue("i", YDB_ERR_CCPNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCPNOTFND", Py_BuildValue("i", YDB_ERR_CCPNOTFND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OPRCCPSTOP", Py_BuildValue("i", YDB_ERR_OPRCCPSTOP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SELECTSYNTAX", Py_BuildValue("i", YDB_ERR_SELECTSYNTAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADABORT", Py_BuildValue("i", YDB_ERR_LOADABORT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FNOTONSYS", Py_BuildValue("i", YDB_ERR_FNOTONSYS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_AMBISYIPARAM", Py_BuildValue("i", YDB_ERR_AMBISYIPARAM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PREVJNLNOEOF", Py_BuildValue("i", YDB_ERR_PREVJNLNOEOF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LKSECINIT", Py_BuildValue("i", YDB_ERR_LKSECINIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG519", Py_BuildValue("i", YDB_ERR_UNUSEDMSG519));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG520", Py_BuildValue("i", YDB_ERR_UNUSEDMSG520));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG521", Py_BuildValue("i", YDB_ERR_UNUSEDMSG521));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TXTSRCMAT", Py_BuildValue("i", YDB_ERR_TXTSRCMAT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCENOGROUP", Py_BuildValue("i", YDB_ERR_CCENOGROUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADDBVER", Py_BuildValue("i", YDB_ERR_BADDBVER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LINKVERSION", Py_BuildValue("i", YDB_ERR_LINKVERSION));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TOTALBLKMAX", Py_BuildValue("i", YDB_ERR_TOTALBLKMAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADCTRLY", Py_BuildValue("i", YDB_ERR_LOADCTRLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CLSTCONFLICT", Py_BuildValue("i", YDB_ERR_CLSTCONFLICT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRCNAM", Py_BuildValue("i", YDB_ERR_SRCNAM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LCKGONE", Py_BuildValue("i", YDB_ERR_LCKGONE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SUB2LONG", Py_BuildValue("i", YDB_ERR_SUB2LONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTRACTCTRLY", Py_BuildValue("i", YDB_ERR_EXTRACTCTRLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCENOWORLD", Py_BuildValue("i", YDB_ERR_CCENOWORLD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVQUERYFAIL", Py_BuildValue("i", YDB_ERR_GVQUERYFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LCKSCANCELLED", Py_BuildValue("i", YDB_ERR_LCKSCANCELLED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVNETFILNM", Py_BuildValue("i", YDB_ERR_INVNETFILNM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NETDBOPNERR", Py_BuildValue("i", YDB_ERR_NETDBOPNERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADSRVRNETMSG", Py_BuildValue("i", YDB_ERR_BADSRVRNETMSG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADGTMNETMSG", Py_BuildValue("i", YDB_ERR_BADGTMNETMSG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SERVERERR", Py_BuildValue("i", YDB_ERR_SERVERERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NETFAIL", Py_BuildValue("i", YDB_ERR_NETFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NETLCKFAIL", Py_BuildValue("i", YDB_ERR_NETLCKFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TTINVFILTER", Py_BuildValue("i", YDB_ERR_TTINVFILTER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG544", Py_BuildValue("i", YDB_ERR_UNUSEDMSG544));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG545", Py_BuildValue("i", YDB_ERR_UNUSEDMSG545));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADTRNPARAM", Py_BuildValue("i", YDB_ERR_BADTRNPARAM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSEONLYBGMM", Py_BuildValue("i", YDB_ERR_DSEONLYBGMM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSEINVLCLUSFN", Py_BuildValue("i", YDB_ERR_DSEINVLCLUSFN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RDFLTOOSHORT", Py_BuildValue("i", YDB_ERR_RDFLTOOSHORT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TIMRBADVAL", Py_BuildValue("i", YDB_ERR_TIMRBADVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCENOSYSLCK", Py_BuildValue("i", YDB_ERR_CCENOSYSLCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCPGRP", Py_BuildValue("i", YDB_ERR_CCPGRP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNSOLCNTERR", Py_BuildValue("i", YDB_ERR_UNSOLCNTERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BACKUPCTRL", Py_BuildValue("i", YDB_ERR_BACKUPCTRL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOCCPPID", Py_BuildValue("i", YDB_ERR_NOCCPPID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCPJNLOPNERR", Py_BuildValue("i", YDB_ERR_CCPJNLOPNERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LCKSGONE", Py_BuildValue("i", YDB_ERR_LCKSGONE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG560", Py_BuildValue("i", YDB_ERR_UNUSEDMSG560));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFILOPERR", Py_BuildValue("i", YDB_ERR_DBFILOPERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCERDERR", Py_BuildValue("i", YDB_ERR_CCERDERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEDBCL", Py_BuildValue("i", YDB_ERR_CCEDBCL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEDBNTCL", Py_BuildValue("i", YDB_ERR_CCEDBNTCL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEWRTERR", Py_BuildValue("i", YDB_ERR_CCEWRTERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEBADFN", Py_BuildValue("i", YDB_ERR_CCEBADFN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCERDTIMOUT", Py_BuildValue("i", YDB_ERR_CCERDTIMOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCPSIGCONT", Py_BuildValue("i", YDB_ERR_CCPSIGCONT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCEBGONLY", Py_BuildValue("i", YDB_ERR_CCEBGONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCENOCCP", Py_BuildValue("i", YDB_ERR_CCENOCCP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCECCPPID", Py_BuildValue("i", YDB_ERR_CCECCPPID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCECLSTPRCS", Py_BuildValue("i", YDB_ERR_CCECLSTPRCS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZSHOWBADFUNC", Py_BuildValue("i", YDB_ERR_ZSHOWBADFUNC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTALLJNLEN", Py_BuildValue("i", YDB_ERR_NOTALLJNLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADLOCKNEST", Py_BuildValue("i", YDB_ERR_BADLOCKNEST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOLBRSRC", Py_BuildValue("i", YDB_ERR_NOLBRSRC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVZSTEP", Py_BuildValue("i", YDB_ERR_INVZSTEP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZSTEPARG", Py_BuildValue("i", YDB_ERR_ZSTEPARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVSTRLEN", Py_BuildValue("i", YDB_ERR_INVSTRLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RECCNT", Py_BuildValue("i", YDB_ERR_RECCNT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TEXT", Py_BuildValue("i", YDB_ERR_TEXT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZWRSPONE", Py_BuildValue("i", YDB_ERR_ZWRSPONE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILEDEL", Py_BuildValue("i", YDB_ERR_FILEDEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLBADLABEL", Py_BuildValue("i", YDB_ERR_JNLBADLABEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLREADEOF", Py_BuildValue("i", YDB_ERR_JNLREADEOF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLRECFMT", Py_BuildValue("i", YDB_ERR_JNLRECFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BLKTOODEEP", Py_BuildValue("i", YDB_ERR_BLKTOODEEP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NESTFORMP", Py_BuildValue("i", YDB_ERR_NESTFORMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BINHDR", Py_BuildValue("i", YDB_ERR_BINHDR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GOQPREC", Py_BuildValue("i", YDB_ERR_GOQPREC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LDGOQFMT", Py_BuildValue("i", YDB_ERR_LDGOQFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BEGINST", Py_BuildValue("i", YDB_ERR_BEGINST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVMVXSZ", Py_BuildValue("i", YDB_ERR_INVMVXSZ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLWRTNOWWRTR", Py_BuildValue("i", YDB_ERR_JNLWRTNOWWRTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSHMCONCPROC", Py_BuildValue("i", YDB_ERR_GTMSECSHRSHMCONCPROC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLINVALLOC", Py_BuildValue("i", YDB_ERR_JNLINVALLOC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLINVEXT", Py_BuildValue("i", YDB_ERR_JNLINVEXT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUPCLIERR", Py_BuildValue("i", YDB_ERR_MUPCLIERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLTMQUAL4", Py_BuildValue("i", YDB_ERR_JNLTMQUAL4));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRREMSHM", Py_BuildValue("i", YDB_ERR_GTMSECSHRREMSHM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRREMFILE", Py_BuildValue("i", YDB_ERR_GTMSECSHRREMFILE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUNODBNAME", Py_BuildValue("i", YDB_ERR_MUNODBNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILECREATE", Py_BuildValue("i", YDB_ERR_FILECREATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILENOTCREATE", Py_BuildValue("i", YDB_ERR_FILENOTCREATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLPROCSTUCK", Py_BuildValue("i", YDB_ERR_JNLPROCSTUCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVGLOBALQUAL", Py_BuildValue("i", YDB_ERR_INVGLOBALQUAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COLLARGLONG", Py_BuildValue("i", YDB_ERR_COLLARGLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOPINI", Py_BuildValue("i", YDB_ERR_NOPINI));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBNOCRE", Py_BuildValue("i", YDB_ERR_DBNOCRE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSPACELOW", Py_BuildValue("i", YDB_ERR_JNLSPACELOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCOMMITCLNUP", Py_BuildValue("i", YDB_ERR_DBCOMMITCLNUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BFRQUALREQ", Py_BuildValue("i", YDB_ERR_BFRQUALREQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REQDVIEWPARM", Py_BuildValue("i", YDB_ERR_REQDVIEWPARM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COLLFNMISSING", Py_BuildValue("i", YDB_ERR_COLLFNMISSING));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLACTINCMPLT", Py_BuildValue("i", YDB_ERR_JNLACTINCMPLT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NCTCOLLDIFF", Py_BuildValue("i", YDB_ERR_NCTCOLLDIFF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLRCUNXEOR", Py_BuildValue("i", YDB_ERR_DLRCUNXEOR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLRCTOOBIG", Py_BuildValue("i", YDB_ERR_DLRCTOOBIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WCERRNOTCHG", Py_BuildValue("i", YDB_ERR_WCERRNOTCHG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WCWRNNOTCHG", Py_BuildValue("i", YDB_ERR_WCWRNNOTCHG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCWRONGDESC", Py_BuildValue("i", YDB_ERR_ZCWRONGDESC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTNWARN", Py_BuildValue("i", YDB_ERR_MUTNWARN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRUPDDBHDR", Py_BuildValue("i", YDB_ERR_GTMSECSHRUPDDBHDR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LCKSTIMOUT", Py_BuildValue("i", YDB_ERR_LCKSTIMOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CTLMNEMAXLEN", Py_BuildValue("i", YDB_ERR_CTLMNEMAXLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CTLMNEXPECTED", Py_BuildValue("i", YDB_ERR_CTLMNEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_USRIOINIT", Py_BuildValue("i", YDB_ERR_USRIOINIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRITSEMFAIL", Py_BuildValue("i", YDB_ERR_CRITSEMFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TERMWRITE", Py_BuildValue("i", YDB_ERR_TERMWRITE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COLLTYPVERSION", Py_BuildValue("i", YDB_ERR_COLLTYPVERSION));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LVNULLSUBS", Py_BuildValue("i", YDB_ERR_LVNULLSUBS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVREPLERR", Py_BuildValue("i", YDB_ERR_GVREPLERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG631", Py_BuildValue("i", YDB_ERR_UNUSEDMSG631));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RMWIDTHPOS", Py_BuildValue("i", YDB_ERR_RMWIDTHPOS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OFFSETINV", Py_BuildValue("i", YDB_ERR_OFFSETINV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBPARTOOLONG", Py_BuildValue("i", YDB_ERR_JOBPARTOOLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG637", Py_BuildValue("i", YDB_ERR_UNUSEDMSG637));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RUNPARAMERR", Py_BuildValue("i", YDB_ERR_RUNPARAMERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FNNAMENEG", Py_BuildValue("i", YDB_ERR_FNNAMENEG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORDER2", Py_BuildValue("i", YDB_ERR_ORDER2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUNOUPGRD", Py_BuildValue("i", YDB_ERR_MUNOUPGRD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REORGCTRLY", Py_BuildValue("i", YDB_ERR_REORGCTRLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TSTRTPARM", Py_BuildValue("i", YDB_ERR_TSTRTPARM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGNAMENF", Py_BuildValue("i", YDB_ERR_TRIGNAMENF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGZBREAKREM", Py_BuildValue("i", YDB_ERR_TRIGZBREAKREM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TLVLZERO", Py_BuildValue("i", YDB_ERR_TLVLZERO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRESTNOT", Py_BuildValue("i", YDB_ERR_TRESTNOT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPLOCK", Py_BuildValue("i", YDB_ERR_TPLOCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPQUIT", Py_BuildValue("i", YDB_ERR_TPQUIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPFAIL", Py_BuildValue("i", YDB_ERR_TPFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPRETRY", Py_BuildValue("i", YDB_ERR_TPRETRY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPTOODEEP", Py_BuildValue("i", YDB_ERR_TPTOODEEP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZDEFACTIVE", Py_BuildValue("i", YDB_ERR_ZDEFACTIVE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZDEFOFLOW", Py_BuildValue("i", YDB_ERR_ZDEFOFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUPRESTERR", Py_BuildValue("i", YDB_ERR_MUPRESTERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUBCKNODIR", Py_BuildValue("i", YDB_ERR_MUBCKNODIR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRANS2BIG", Py_BuildValue("i", YDB_ERR_TRANS2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVBITLEN", Py_BuildValue("i", YDB_ERR_INVBITLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVBITSTR", Py_BuildValue("i", YDB_ERR_INVBITSTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVBITPOS", Py_BuildValue("i", YDB_ERR_INVBITPOS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PARNORMAL", Py_BuildValue("i", YDB_ERR_PARNORMAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PARBUFSM", Py_BuildValue("i", YDB_ERR_PARBUFSM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RMWIDTHTOOBIG", Py_BuildValue("i", YDB_ERR_RMWIDTHTOOBIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATTABNOTFND", Py_BuildValue("i", YDB_ERR_PATTABNOTFND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OBJFILERR", Py_BuildValue("i", YDB_ERR_OBJFILERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRCFILERR", Py_BuildValue("i", YDB_ERR_SRCFILERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NEGFRACPWR", Py_BuildValue("i", YDB_ERR_NEGFRACPWR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MTNOSKIP", Py_BuildValue("i", YDB_ERR_MTNOSKIP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CETOOMANY", Py_BuildValue("i", YDB_ERR_CETOOMANY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CEUSRERROR", Py_BuildValue("i", YDB_ERR_CEUSRERROR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CEBIGSKIP", Py_BuildValue("i", YDB_ERR_CEBIGSKIP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CETOOLONG", Py_BuildValue("i", YDB_ERR_CETOOLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CENOINDIR", Py_BuildValue("i", YDB_ERR_CENOINDIR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COLLATIONUNDEF", Py_BuildValue("i", YDB_ERR_COLLATIONUNDEF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MSTACKCRIT", Py_BuildValue("i", YDB_ERR_MSTACKCRIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSRVF", Py_BuildValue("i", YDB_ERR_GTMSECSHRSRVF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FREEZECTRL", Py_BuildValue("i", YDB_ERR_FREEZECTRL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFLUSH", Py_BuildValue("i", YDB_ERR_JNLFLUSH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CCPSIGDMP", Py_BuildValue("i", YDB_ERR_CCPSIGDMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOPRINCIO", Py_BuildValue("i", YDB_ERR_NOPRINCIO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVPORTSPEC", Py_BuildValue("i", YDB_ERR_INVPORTSPEC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVADDRSPEC", Py_BuildValue("i", YDB_ERR_INVADDRSPEC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUREENCRYPTEND", Py_BuildValue("i", YDB_ERR_MUREENCRYPTEND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTJNLMISMATCH", Py_BuildValue("i", YDB_ERR_CRYPTJNLMISMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKWAIT", Py_BuildValue("i", YDB_ERR_SOCKWAIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKACPT", Py_BuildValue("i", YDB_ERR_SOCKACPT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKINIT", Py_BuildValue("i", YDB_ERR_SOCKINIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OPENCONN", Py_BuildValue("i", YDB_ERR_OPENCONN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVNOTIMP", Py_BuildValue("i", YDB_ERR_DEVNOTIMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATALTER2LARGE", Py_BuildValue("i", YDB_ERR_PATALTER2LARGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBREMOTE", Py_BuildValue("i", YDB_ERR_DBREMOTE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLREQUIRED", Py_BuildValue("i", YDB_ERR_JNLREQUIRED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPMIXUP", Py_BuildValue("i", YDB_ERR_TPMIXUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_HTOFLOW", Py_BuildValue("i", YDB_ERR_HTOFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RMNOBIGRECORD", Py_BuildValue("i", YDB_ERR_RMNOBIGRECORD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBMSIZE", Py_BuildValue("i", YDB_ERR_DBBMSIZE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBMBARE", Py_BuildValue("i", YDB_ERR_DBBMBARE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBMINV", Py_BuildValue("i", YDB_ERR_DBBMINV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBMMSTR", Py_BuildValue("i", YDB_ERR_DBBMMSTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBROOTBURN", Py_BuildValue("i", YDB_ERR_DBROOTBURN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLSTATEERR", Py_BuildValue("i", YDB_ERR_REPLSTATEERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG702", Py_BuildValue("i", YDB_ERR_UNUSEDMSG702));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBDIRTSUBSC", Py_BuildValue("i", YDB_ERR_DBDIRTSUBSC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TIMEROVFL", Py_BuildValue("i", YDB_ERR_TIMEROVFL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMASSERT", Py_BuildValue("i", YDB_ERR_GTMASSERT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFHEADERR4", Py_BuildValue("i", YDB_ERR_DBFHEADERR4));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBADDRANGE", Py_BuildValue("i", YDB_ERR_DBADDRANGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBQUELINK", Py_BuildValue("i", YDB_ERR_DBQUELINK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCRERR", Py_BuildValue("i", YDB_ERR_DBCRERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUSTANDALONE", Py_BuildValue("i", YDB_ERR_MUSTANDALONE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUNOACTION", Py_BuildValue("i", YDB_ERR_MUNOACTION));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RMBIGSHARE", Py_BuildValue("i", YDB_ERR_RMBIGSHARE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPRESTART", Py_BuildValue("i", YDB_ERR_TPRESTART));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKWRITE", Py_BuildValue("i", YDB_ERR_SOCKWRITE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCNTRLERR", Py_BuildValue("i", YDB_ERR_DBCNTRLERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTERMENV", Py_BuildValue("i", YDB_ERR_NOTERMENV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTERMENTRY", Py_BuildValue("i", YDB_ERR_NOTERMENTRY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTERMINFODB", Py_BuildValue("i", YDB_ERR_NOTERMINFODB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVACCMETHOD", Py_BuildValue("i", YDB_ERR_INVACCMETHOD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLOPNERR", Py_BuildValue("i", YDB_ERR_JNLOPNERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLRECTYPE", Py_BuildValue("i", YDB_ERR_JNLRECTYPE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLTRANSGTR", Py_BuildValue("i", YDB_ERR_JNLTRANSGTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLTRANSLSS", Py_BuildValue("i", YDB_ERR_JNLTRANSLSS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLWRERR", Py_BuildValue("i", YDB_ERR_JNLWRERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILEIDMATCH", Py_BuildValue("i", YDB_ERR_FILEIDMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTSRCLIN", Py_BuildValue("i", YDB_ERR_EXTSRCLIN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTSRCLOC", Py_BuildValue("i", YDB_ERR_EXTSRCLOC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG728", Py_BuildValue("i", YDB_ERR_UNUSEDMSG728));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ERRCALL", Py_BuildValue("i", YDB_ERR_ERRCALL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCCTENV", Py_BuildValue("i", YDB_ERR_ZCCTENV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCCTOPN", Py_BuildValue("i", YDB_ERR_ZCCTOPN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCCTNULLF", Py_BuildValue("i", YDB_ERR_ZCCTNULLF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCUNAVAIL", Py_BuildValue("i", YDB_ERR_ZCUNAVAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCENTNAME", Py_BuildValue("i", YDB_ERR_ZCENTNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCCOLON", Py_BuildValue("i", YDB_ERR_ZCCOLON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCRTNTYP", Py_BuildValue("i", YDB_ERR_ZCRTNTYP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCRCALLNAME", Py_BuildValue("i", YDB_ERR_ZCRCALLNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCRPARMNAME", Py_BuildValue("i", YDB_ERR_ZCRPARMNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCUNTYPE", Py_BuildValue("i", YDB_ERR_ZCUNTYPE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCMLTSTATUS", Py_BuildValue("i", YDB_ERR_ZCMLTSTATUS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCSTATUSRET", Py_BuildValue("i", YDB_ERR_ZCSTATUSRET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCMAXPARAM", Py_BuildValue("i", YDB_ERR_ZCMAXPARAM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCCSQRBR", Py_BuildValue("i", YDB_ERR_ZCCSQRBR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCPREALLNUMEX", Py_BuildValue("i", YDB_ERR_ZCPREALLNUMEX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCPREALLVALPAR", Py_BuildValue("i", YDB_ERR_ZCPREALLVALPAR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VERMISMATCH", Py_BuildValue("i", YDB_ERR_VERMISMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLCNTRL", Py_BuildValue("i", YDB_ERR_JNLCNTRL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGNAMBAD", Py_BuildValue("i", YDB_ERR_TRIGNAMBAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BUFRDTIMEOUT", Py_BuildValue("i", YDB_ERR_BUFRDTIMEOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVALIDRIP", Py_BuildValue("i", YDB_ERR_INVALIDRIP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BLKSIZ512", Py_BuildValue("i", YDB_ERR_BLKSIZ512));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTEXERR", Py_BuildValue("i", YDB_ERR_MUTEXERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLVSIZE", Py_BuildValue("i", YDB_ERR_JNLVSIZE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTEXLCKALERT", Py_BuildValue("i", YDB_ERR_MUTEXLCKALERT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTEXFRCDTERM", Py_BuildValue("i", YDB_ERR_MUTEXFRCDTERM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHR", Py_BuildValue("i", YDB_ERR_GTMSECSHR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSRVFID", Py_BuildValue("i", YDB_ERR_GTMSECSHRSRVFID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSRVFIL", Py_BuildValue("i", YDB_ERR_GTMSECSHRSRVFIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FREEBLKSLOW", Py_BuildValue("i", YDB_ERR_FREEBLKSLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PROTNOTSUP", Py_BuildValue("i", YDB_ERR_PROTNOTSUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DELIMSIZNA", Py_BuildValue("i", YDB_ERR_DELIMSIZNA));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVCTLMNE", Py_BuildValue("i", YDB_ERR_INVCTLMNE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKLISTEN", Py_BuildValue("i", YDB_ERR_SOCKLISTEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG762", Py_BuildValue("i", YDB_ERR_UNUSEDMSG762));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ADDRTOOLONG", Py_BuildValue("i", YDB_ERR_ADDRTOOLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRGETSEMFAIL", Py_BuildValue("i", YDB_ERR_GTMSECSHRGETSEMFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CPBEYALLOC", Py_BuildValue("i", YDB_ERR_CPBEYALLOC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRDONLY", Py_BuildValue("i", YDB_ERR_DBRDONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DUPTN", Py_BuildValue("i", YDB_ERR_DUPTN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRESTLOC", Py_BuildValue("i", YDB_ERR_TRESTLOC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLPOOLINST", Py_BuildValue("i", YDB_ERR_REPLPOOLINST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCVECTORINDX", Py_BuildValue("i", YDB_ERR_ZCVECTORINDX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLNOTON", Py_BuildValue("i", YDB_ERR_REPLNOTON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLMOVED", Py_BuildValue("i", YDB_ERR_JNLMOVED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTRFMT", Py_BuildValue("i", YDB_ERR_EXTRFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CALLERID", Py_BuildValue("i", YDB_ERR_CALLERID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_KRNLKILL", Py_BuildValue("i", YDB_ERR_KRNLKILL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MEMORYRECURSIVE", Py_BuildValue("i", YDB_ERR_MEMORYRECURSIVE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FREEZEID", Py_BuildValue("i", YDB_ERR_FREEZEID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BLKWRITERR", Py_BuildValue("i", YDB_ERR_BLKWRITERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG781", Py_BuildValue("i", YDB_ERR_UNUSEDMSG781));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PINENTRYERR", Py_BuildValue("i", YDB_ERR_PINENTRYERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BCKUPBUFLUSH", Py_BuildValue("i", YDB_ERR_BCKUPBUFLUSH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOFORKCORE", Py_BuildValue("i", YDB_ERR_NOFORKCORE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLREAD", Py_BuildValue("i", YDB_ERR_JNLREAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLMINALIGN", Py_BuildValue("i", YDB_ERR_JNLMINALIGN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBSTARTCMDFAIL", Py_BuildValue("i", YDB_ERR_JOBSTARTCMDFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLPOOLSETUP", Py_BuildValue("i", YDB_ERR_JNLPOOLSETUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSTATEOFF", Py_BuildValue("i", YDB_ERR_JNLSTATEOFF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RECVPOOLSETUP", Py_BuildValue("i", YDB_ERR_RECVPOOLSETUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLCOMM", Py_BuildValue("i", YDB_ERR_REPLCOMM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOREPLCTDREG", Py_BuildValue("i", YDB_ERR_NOREPLCTDREG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINFO", Py_BuildValue("i", YDB_ERR_REPLINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLWARN", Py_BuildValue("i", YDB_ERR_REPLWARN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLERR", Py_BuildValue("i", YDB_ERR_REPLERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLNMBKNOTPRCD", Py_BuildValue("i", YDB_ERR_JNLNMBKNOTPRCD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLFILIOERR", Py_BuildValue("i", YDB_ERR_REPLFILIOERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLBRKNTRANS", Py_BuildValue("i", YDB_ERR_REPLBRKNTRANS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TTWIDTHTOOBIG", Py_BuildValue("i", YDB_ERR_TTWIDTHTOOBIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLLOGOPN", Py_BuildValue("i", YDB_ERR_REPLLOGOPN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLFILTER", Py_BuildValue("i", YDB_ERR_REPLFILTER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GBLMODFAIL", Py_BuildValue("i", YDB_ERR_GBLMODFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TTLENGTHTOOBIG", Py_BuildValue("i", YDB_ERR_TTLENGTHTOOBIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPTIMEOUT", Py_BuildValue("i", YDB_ERR_TPTIMEOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEFEREVENT", Py_BuildValue("i", YDB_ERR_DEFEREVENT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFILNOTCHG", Py_BuildValue("i", YDB_ERR_JNLFILNOTCHG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EVENTLOGERR", Py_BuildValue("i", YDB_ERR_EVENTLOGERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UPDATEFILEOPEN", Py_BuildValue("i", YDB_ERR_UPDATEFILEOPEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLBADRECFMT", Py_BuildValue("i", YDB_ERR_JNLBADRECFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NULLCOLLDIFF", Py_BuildValue("i", YDB_ERR_NULLCOLLDIFF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUKILLIP", Py_BuildValue("i", YDB_ERR_MUKILLIP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLRDONLY", Py_BuildValue("i", YDB_ERR_JNLRDONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ANCOMPTINC", Py_BuildValue("i", YDB_ERR_ANCOMPTINC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ABNCOMPTINC", Py_BuildValue("i", YDB_ERR_ABNCOMPTINC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RECLOAD", Py_BuildValue("i", YDB_ERR_RECLOAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKNOTFND", Py_BuildValue("i", YDB_ERR_SOCKNOTFND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CURRSOCKOFR", Py_BuildValue("i", YDB_ERR_CURRSOCKOFR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKETEXIST", Py_BuildValue("i", YDB_ERR_SOCKETEXIST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LISTENPASSBND", Py_BuildValue("i", YDB_ERR_LISTENPASSBND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCLNUPINFO", Py_BuildValue("i", YDB_ERR_DBCLNUPINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUNODWNGRD", Py_BuildValue("i", YDB_ERR_MUNODWNGRD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLTRANS2BIG", Py_BuildValue("i", YDB_ERR_REPLTRANS2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RDFLTOOLONG", Py_BuildValue("i", YDB_ERR_RDFLTOOLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUNOFINISH", Py_BuildValue("i", YDB_ERR_MUNOFINISH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFILEXT", Py_BuildValue("i", YDB_ERR_DBFILEXT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFSYNCERR", Py_BuildValue("i", YDB_ERR_JNLFSYNCERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ICUNOTENABLED", Py_BuildValue("i", YDB_ERR_ICUNOTENABLED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCPREALLVALINV", Py_BuildValue("i", YDB_ERR_ZCPREALLVALINV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NEWJNLFILECREAT", Py_BuildValue("i", YDB_ERR_NEWJNLFILECREAT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSKSPACEFLOW", Py_BuildValue("i", YDB_ERR_DSKSPACEFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVINCRFAIL", Py_BuildValue("i", YDB_ERR_GVINCRFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ISOLATIONSTSCHN", Py_BuildValue("i", YDB_ERR_ISOLATIONSTSCHN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG833", Py_BuildValue("i", YDB_ERR_UNUSEDMSG833));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRACEON", Py_BuildValue("i", YDB_ERR_TRACEON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TOOMANYCLIENTS", Py_BuildValue("i", YDB_ERR_TOOMANYCLIENTS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOEXCLUDE", Py_BuildValue("i", YDB_ERR_NOEXCLUDE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG837", Py_BuildValue("i", YDB_ERR_UNUSEDMSG837));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXCLUDEREORG", Py_BuildValue("i", YDB_ERR_EXCLUDEREORG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REORGINC", Py_BuildValue("i", YDB_ERR_REORGINC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ASC2EBCDICCONV", Py_BuildValue("i", YDB_ERR_ASC2EBCDICCONV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSTART", Py_BuildValue("i", YDB_ERR_GTMSECSHRSTART));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBVERPERFWARN1", Py_BuildValue("i", YDB_ERR_DBVERPERFWARN1));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILEIDGBLSEC", Py_BuildValue("i", YDB_ERR_FILEIDGBLSEC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GBLSECNOTGDS", Py_BuildValue("i", YDB_ERR_GBLSECNOTGDS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADGBLSECVER", Py_BuildValue("i", YDB_ERR_BADGBLSECVER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RECSIZENOTEVEN", Py_BuildValue("i", YDB_ERR_RECSIZENOTEVEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BUFFLUFAILED", Py_BuildValue("i", YDB_ERR_BUFFLUFAILED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUQUALINCOMP", Py_BuildValue("i", YDB_ERR_MUQUALINCOMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DISTPATHMAX", Py_BuildValue("i", YDB_ERR_DISTPATHMAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILEOPENFAIL", Py_BuildValue("i", YDB_ERR_FILEOPENFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG851", Py_BuildValue("i", YDB_ERR_UNUSEDMSG851));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRPERM", Py_BuildValue("i", YDB_ERR_GTMSECSHRPERM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_YDBDISTUNDEF", Py_BuildValue("i", YDB_ERR_YDBDISTUNDEF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SYSCALL", Py_BuildValue("i", YDB_ERR_SYSCALL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXGTMPATH", Py_BuildValue("i", YDB_ERR_MAXGTMPATH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TROLLBK2DEEP", Py_BuildValue("i", YDB_ERR_TROLLBK2DEEP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVROLLBKLVL", Py_BuildValue("i", YDB_ERR_INVROLLBKLVL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OLDBINEXTRACT", Py_BuildValue("i", YDB_ERR_OLDBINEXTRACT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ACOMPTBINC", Py_BuildValue("i", YDB_ERR_ACOMPTBINC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTREPLICATED", Py_BuildValue("i", YDB_ERR_NOTREPLICATED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBPREMATEOF", Py_BuildValue("i", YDB_ERR_DBPREMATEOF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_KILLBYSIG", Py_BuildValue("i", YDB_ERR_KILLBYSIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_KILLBYSIGUINFO", Py_BuildValue("i", YDB_ERR_KILLBYSIGUINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_KILLBYSIGSINFO1", Py_BuildValue("i", YDB_ERR_KILLBYSIGSINFO1));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_KILLBYSIGSINFO2", Py_BuildValue("i", YDB_ERR_KILLBYSIGSINFO2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGILLOPC", Py_BuildValue("i", YDB_ERR_SIGILLOPC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGILLOPN", Py_BuildValue("i", YDB_ERR_SIGILLOPN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGILLADR", Py_BuildValue("i", YDB_ERR_SIGILLADR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGILLTRP", Py_BuildValue("i", YDB_ERR_SIGILLTRP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGPRVOPC", Py_BuildValue("i", YDB_ERR_SIGPRVOPC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGPRVREG", Py_BuildValue("i", YDB_ERR_SIGPRVREG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGCOPROC", Py_BuildValue("i", YDB_ERR_SIGCOPROC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGBADSTK", Py_BuildValue("i", YDB_ERR_SIGBADSTK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGADRALN", Py_BuildValue("i", YDB_ERR_SIGADRALN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGADRERR", Py_BuildValue("i", YDB_ERR_SIGADRERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGOBJERR", Py_BuildValue("i", YDB_ERR_SIGOBJERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGINTDIV", Py_BuildValue("i", YDB_ERR_SIGINTDIV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGINTOVF", Py_BuildValue("i", YDB_ERR_SIGINTOVF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGFLTDIV", Py_BuildValue("i", YDB_ERR_SIGFLTDIV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGFLTOVF", Py_BuildValue("i", YDB_ERR_SIGFLTOVF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGFLTUND", Py_BuildValue("i", YDB_ERR_SIGFLTUND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGFLTRES", Py_BuildValue("i", YDB_ERR_SIGFLTRES));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGFLTINV", Py_BuildValue("i", YDB_ERR_SIGFLTINV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGMAPERR", Py_BuildValue("i", YDB_ERR_SIGMAPERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIGACCERR", Py_BuildValue("i", YDB_ERR_SIGACCERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRNLOGFAIL", Py_BuildValue("i", YDB_ERR_TRNLOGFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVDBGLVL", Py_BuildValue("i", YDB_ERR_INVDBGLVL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMAXNRSUBS", Py_BuildValue("i", YDB_ERR_DBMAXNRSUBS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSCKSEL", Py_BuildValue("i", YDB_ERR_GTMSECSHRSCKSEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRTMOUT", Py_BuildValue("i", YDB_ERR_GTMSECSHRTMOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRRECVF", Py_BuildValue("i", YDB_ERR_GTMSECSHRRECVF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSENDF", Py_BuildValue("i", YDB_ERR_GTMSECSHRSENDF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIZENOTVALID8", Py_BuildValue("i", YDB_ERR_SIZENOTVALID8));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHROPCMP", Py_BuildValue("i", YDB_ERR_GTMSECSHROPCMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSUIDF", Py_BuildValue("i", YDB_ERR_GTMSECSHRSUIDF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSGIDF", Py_BuildValue("i", YDB_ERR_GTMSECSHRSGIDF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSSIDF", Py_BuildValue("i", YDB_ERR_GTMSECSHRSSIDF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRFORKF", Py_BuildValue("i", YDB_ERR_GTMSECSHRFORKF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFSYNCERR", Py_BuildValue("i", YDB_ERR_DBFSYNCERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SECONDAHEAD", Py_BuildValue("i", YDB_ERR_SECONDAHEAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SCNDDBNOUPD", Py_BuildValue("i", YDB_ERR_SCNDDBNOUPD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUINFOUINT4", Py_BuildValue("i", YDB_ERR_MUINFOUINT4));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NLMISMATCHCALC", Py_BuildValue("i", YDB_ERR_NLMISMATCHCALC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RELINKCTLFULL", Py_BuildValue("i", YDB_ERR_RELINKCTLFULL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUPIPSET2BIG", Py_BuildValue("i", YDB_ERR_MUPIPSET2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBADNSUB", Py_BuildValue("i", YDB_ERR_DBBADNSUB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBADKYNM", Py_BuildValue("i", YDB_ERR_DBBADKYNM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBADPNTR", Py_BuildValue("i", YDB_ERR_DBBADPNTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBNPNTR", Py_BuildValue("i", YDB_ERR_DBBNPNTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBINCLVL", Py_BuildValue("i", YDB_ERR_DBINCLVL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBFSTAT", Py_BuildValue("i", YDB_ERR_DBBFSTAT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBDBALLOC", Py_BuildValue("i", YDB_ERR_DBBDBALLOC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMRKFREE", Py_BuildValue("i", YDB_ERR_DBMRKFREE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMRKBUSY", Py_BuildValue("i", YDB_ERR_DBMRKBUSY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBSIZZRO", Py_BuildValue("i", YDB_ERR_DBBSIZZRO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBSZGT64K", Py_BuildValue("i", YDB_ERR_DBSZGT64K));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBNOTMLTP", Py_BuildValue("i", YDB_ERR_DBNOTMLTP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBTNTOOLG", Py_BuildValue("i", YDB_ERR_DBTNTOOLG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBPLMLT512", Py_BuildValue("i", YDB_ERR_DBBPLMLT512));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBPLMGT2K", Py_BuildValue("i", YDB_ERR_DBBPLMGT2K));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUINFOUINT8", Py_BuildValue("i", YDB_ERR_MUINFOUINT8));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBPLNOT512", Py_BuildValue("i", YDB_ERR_DBBPLNOT512));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUINFOSTR", Py_BuildValue("i", YDB_ERR_MUINFOSTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBUNDACCMT", Py_BuildValue("i", YDB_ERR_DBUNDACCMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBTNNEQ", Py_BuildValue("i", YDB_ERR_DBTNNEQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUPGRDSUCC", Py_BuildValue("i", YDB_ERR_MUPGRDSUCC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBDSRDFMTCHNG", Py_BuildValue("i", YDB_ERR_DBDSRDFMTCHNG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFGTBC", Py_BuildValue("i", YDB_ERR_DBFGTBC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFSTBC", Py_BuildValue("i", YDB_ERR_DBFSTBC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFSTHEAD", Py_BuildValue("i", YDB_ERR_DBFSTHEAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCREINCOMP", Py_BuildValue("i", YDB_ERR_DBCREINCOMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFLCORRP", Py_BuildValue("i", YDB_ERR_DBFLCORRP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBHEADINV", Py_BuildValue("i", YDB_ERR_DBHEADINV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBINCRVER", Py_BuildValue("i", YDB_ERR_DBINCRVER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBINVGBL", Py_BuildValue("i", YDB_ERR_DBINVGBL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBKEYGTIND", Py_BuildValue("i", YDB_ERR_DBKEYGTIND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBGTDBMAX", Py_BuildValue("i", YDB_ERR_DBGTDBMAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBKGTALLW", Py_BuildValue("i", YDB_ERR_DBKGTALLW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBLTSIBL", Py_BuildValue("i", YDB_ERR_DBLTSIBL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBLRCINVSZ", Py_BuildValue("i", YDB_ERR_DBLRCINVSZ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUREUPDWNGRDEND", Py_BuildValue("i", YDB_ERR_MUREUPDWNGRDEND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBLOCMBINC", Py_BuildValue("i", YDB_ERR_DBLOCMBINC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBLVLINC", Py_BuildValue("i", YDB_ERR_DBLVLINC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBSIZMX", Py_BuildValue("i", YDB_ERR_DBMBSIZMX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBSIZMN", Py_BuildValue("i", YDB_ERR_DBMBSIZMN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBTNSIZMX", Py_BuildValue("i", YDB_ERR_DBMBTNSIZMX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBMINCFRE", Py_BuildValue("i", YDB_ERR_DBMBMINCFRE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBPINCFL", Py_BuildValue("i", YDB_ERR_DBMBPINCFL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBPFLDLBM", Py_BuildValue("i", YDB_ERR_DBMBPFLDLBM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBPFLINT", Py_BuildValue("i", YDB_ERR_DBMBPFLINT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBPFLDIS", Py_BuildValue("i", YDB_ERR_DBMBPFLDIS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBPFRDLBM", Py_BuildValue("i", YDB_ERR_DBMBPFRDLBM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBPFRINT", Py_BuildValue("i", YDB_ERR_DBMBPFRINT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMAXKEYEXC", Py_BuildValue("i", YDB_ERR_DBMAXKEYEXC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMXRSEXCMIN", Py_BuildValue("i", YDB_ERR_DBMXRSEXCMIN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUPIPSET2SML", Py_BuildValue("i", YDB_ERR_MUPIPSET2SML));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBREADBM", Py_BuildValue("i", YDB_ERR_DBREADBM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCOMPTOOLRG", Py_BuildValue("i", YDB_ERR_DBCOMPTOOLRG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBVERPERFWARN2", Py_BuildValue("i", YDB_ERR_DBVERPERFWARN2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRBNTOOLRG", Py_BuildValue("i", YDB_ERR_DBRBNTOOLRG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRBNLBMN", Py_BuildValue("i", YDB_ERR_DBRBNLBMN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRBNNEG", Py_BuildValue("i", YDB_ERR_DBRBNNEG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRLEVTOOHI", Py_BuildValue("i", YDB_ERR_DBRLEVTOOHI));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRLEVLTONE", Py_BuildValue("i", YDB_ERR_DBRLEVLTONE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBSVBNMIN", Py_BuildValue("i", YDB_ERR_DBSVBNMIN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBTTLBLK0", Py_BuildValue("i", YDB_ERR_DBTTLBLK0));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBNOTDB", Py_BuildValue("i", YDB_ERR_DBNOTDB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBTOTBLK", Py_BuildValue("i", YDB_ERR_DBTOTBLK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBTN", Py_BuildValue("i", YDB_ERR_DBTN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBNOREGION", Py_BuildValue("i", YDB_ERR_DBNOREGION));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBTNRESETINC", Py_BuildValue("i", YDB_ERR_DBTNRESETINC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBTNLTCTN", Py_BuildValue("i", YDB_ERR_DBTNLTCTN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBTNRESET", Py_BuildValue("i", YDB_ERR_DBTNRESET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTEXRSRCCLNUP", Py_BuildValue("i", YDB_ERR_MUTEXRSRCCLNUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SEMWT2LONG", Py_BuildValue("i", YDB_ERR_SEMWT2LONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTOPEN", Py_BuildValue("i", YDB_ERR_REPLINSTOPEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTCLOSE", Py_BuildValue("i", YDB_ERR_REPLINSTCLOSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBSETUP", Py_BuildValue("i", YDB_ERR_JOBSETUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCRERR8", Py_BuildValue("i", YDB_ERR_DBCRERR8));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NUMPROCESSORS", Py_BuildValue("i", YDB_ERR_NUMPROCESSORS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBADDRANGE8", Py_BuildValue("i", YDB_ERR_DBADDRANGE8));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RNDWNSEMFAIL", Py_BuildValue("i", YDB_ERR_RNDWNSEMFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRSHUTDN", Py_BuildValue("i", YDB_ERR_GTMSECSHRSHUTDN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOSPACECRE", Py_BuildValue("i", YDB_ERR_NOSPACECRE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOWSPACECRE", Py_BuildValue("i", YDB_ERR_LOWSPACECRE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WAITDSKSPACE", Py_BuildValue("i", YDB_ERR_WAITDSKSPACE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OUTOFSPACE", Py_BuildValue("i", YDB_ERR_OUTOFSPACE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLPVTINFO", Py_BuildValue("i", YDB_ERR_JNLPVTINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOSPACEEXT", Py_BuildValue("i", YDB_ERR_NOSPACEEXT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WCBLOCKED", Py_BuildValue("i", YDB_ERR_WCBLOCKED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLJNLCLOSED", Py_BuildValue("i", YDB_ERR_REPLJNLCLOSED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RENAMEFAIL", Py_BuildValue("i", YDB_ERR_RENAMEFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILERENAME", Py_BuildValue("i", YDB_ERR_FILERENAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLBUFINFO", Py_BuildValue("i", YDB_ERR_JNLBUFINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SDSEEKERR", Py_BuildValue("i", YDB_ERR_SDSEEKERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOCALSOCKREQ", Py_BuildValue("i", YDB_ERR_LOCALSOCKREQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPNOTACID", Py_BuildValue("i", YDB_ERR_TPNOTACID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSETDATA2LONG", Py_BuildValue("i", YDB_ERR_JNLSETDATA2LONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLNEWREC", Py_BuildValue("i", YDB_ERR_JNLNEWREC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLFTOKSEM", Py_BuildValue("i", YDB_ERR_REPLFTOKSEM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKNOTPASSED", Py_BuildValue("i", YDB_ERR_SOCKNOTPASSED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1002", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1002));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1003", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1003));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CONNSOCKREQ", Py_BuildValue("i", YDB_ERR_CONNSOCKREQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLEXITERR", Py_BuildValue("i", YDB_ERR_REPLEXITERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUDESTROYSUC", Py_BuildValue("i", YDB_ERR_MUDESTROYSUC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRNDWN", Py_BuildValue("i", YDB_ERR_DBRNDWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUDESTROYFAIL", Py_BuildValue("i", YDB_ERR_MUDESTROYFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTALLDBOPN", Py_BuildValue("i", YDB_ERR_NOTALLDBOPN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUSELFBKUP", Py_BuildValue("i", YDB_ERR_MUSELFBKUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBDANGER", Py_BuildValue("i", YDB_ERR_DBDANGER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1012", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1012));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TCGETATTR", Py_BuildValue("i", YDB_ERR_TCGETATTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TCSETATTR", Py_BuildValue("i", YDB_ERR_TCSETATTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_IOWRITERR", Py_BuildValue("i", YDB_ERR_IOWRITERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTWRITE", Py_BuildValue("i", YDB_ERR_REPLINSTWRITE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBADFREEBLKCTR", Py_BuildValue("i", YDB_ERR_DBBADFREEBLKCTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REQ2RESUME", Py_BuildValue("i", YDB_ERR_REQ2RESUME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TIMERHANDLER", Py_BuildValue("i", YDB_ERR_TIMERHANDLER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FREEMEMORY", Py_BuildValue("i", YDB_ERR_FREEMEMORY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUREPLSECDEL", Py_BuildValue("i", YDB_ERR_MUREPLSECDEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUREPLSECNOTDEL", Py_BuildValue("i", YDB_ERR_MUREPLSECNOTDEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUJPOOLRNDWNSUC", Py_BuildValue("i", YDB_ERR_MUJPOOLRNDWNSUC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MURPOOLRNDWNSUC", Py_BuildValue("i", YDB_ERR_MURPOOLRNDWNSUC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUJPOOLRNDWNFL", Py_BuildValue("i", YDB_ERR_MUJPOOLRNDWNFL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MURPOOLRNDWNFL", Py_BuildValue("i", YDB_ERR_MURPOOLRNDWNFL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUREPLPOOL", Py_BuildValue("i", YDB_ERR_MUREPLPOOL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLACCSEM", Py_BuildValue("i", YDB_ERR_REPLACCSEM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFLUSHNOPROG", Py_BuildValue("i", YDB_ERR_JNLFLUSHNOPROG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTCREATE", Py_BuildValue("i", YDB_ERR_REPLINSTCREATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SUSPENDING", Py_BuildValue("i", YDB_ERR_SUSPENDING));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKBFNOTEMPTY", Py_BuildValue("i", YDB_ERR_SOCKBFNOTEMPTY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ILLESOCKBFSIZE", Py_BuildValue("i", YDB_ERR_ILLESOCKBFSIZE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOSOCKETINDEV", Py_BuildValue("i", YDB_ERR_NOSOCKETINDEV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETSOCKOPTERR", Py_BuildValue("i", YDB_ERR_SETSOCKOPTERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GETSOCKOPTERR", Py_BuildValue("i", YDB_ERR_GETSOCKOPTERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOSUCHPROC", Py_BuildValue("i", YDB_ERR_NOSUCHPROC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSENOFINISH", Py_BuildValue("i", YDB_ERR_DSENOFINISH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LKENOFINISH", Py_BuildValue("i", YDB_ERR_LKENOFINISH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOCHLEFT", Py_BuildValue("i", YDB_ERR_NOCHLEFT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MULOGNAMEDEF", Py_BuildValue("i", YDB_ERR_MULOGNAMEDEF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BUFOWNERSTUCK", Py_BuildValue("i", YDB_ERR_BUFOWNERSTUCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ACTIVATEFAIL", Py_BuildValue("i", YDB_ERR_ACTIVATEFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBRNDWNWRN", Py_BuildValue("i", YDB_ERR_DBRNDWNWRN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLLNOOPEN", Py_BuildValue("i", YDB_ERR_DLLNOOPEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLLNORTN", Py_BuildValue("i", YDB_ERR_DLLNORTN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLLNOCLOSE", Py_BuildValue("i", YDB_ERR_DLLNOCLOSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILTERNOTALIVE", Py_BuildValue("i", YDB_ERR_FILTERNOTALIVE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILTERCOMM", Py_BuildValue("i", YDB_ERR_FILTERCOMM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILTERBADCONV", Py_BuildValue("i", YDB_ERR_FILTERBADCONV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PRIMARYISROOT", Py_BuildValue("i", YDB_ERR_PRIMARYISROOT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVQUERYGETFAIL", Py_BuildValue("i", YDB_ERR_GVQUERYGETFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCREC2BIGINBLK", Py_BuildValue("i", YDB_ERR_DBCREC2BIGINBLK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MERGEDESC", Py_BuildValue("i", YDB_ERR_MERGEDESC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MERGEINCOMPL", Py_BuildValue("i", YDB_ERR_MERGEINCOMPL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBNAMEMISMATCH", Py_BuildValue("i", YDB_ERR_DBNAMEMISMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBIDMISMATCH", Py_BuildValue("i", YDB_ERR_DBIDMISMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVOPENFAIL", Py_BuildValue("i", YDB_ERR_DEVOPENFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_IPCNOTDEL", Py_BuildValue("i", YDB_ERR_IPCNOTDEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_XCVOIDRET", Py_BuildValue("i", YDB_ERR_XCVOIDRET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MURAIMGFAIL", Py_BuildValue("i", YDB_ERR_MURAIMGFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTUNDEF", Py_BuildValue("i", YDB_ERR_REPLINSTUNDEF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTACC", Py_BuildValue("i", YDB_ERR_REPLINSTACC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOJNLPOOL", Py_BuildValue("i", YDB_ERR_NOJNLPOOL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NORECVPOOL", Py_BuildValue("i", YDB_ERR_NORECVPOOL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FTOKERR", Py_BuildValue("i", YDB_ERR_FTOKERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLREQRUNDOWN", Py_BuildValue("i", YDB_ERR_REPLREQRUNDOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BLKCNTEDITFAIL", Py_BuildValue("i", YDB_ERR_BLKCNTEDITFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SEMREMOVED", Py_BuildValue("i", YDB_ERR_SEMREMOVED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTFMT", Py_BuildValue("i", YDB_ERR_REPLINSTFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SEMKEYINUSE", Py_BuildValue("i", YDB_ERR_SEMKEYINUSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_XTRNTRANSERR", Py_BuildValue("i", YDB_ERR_XTRNTRANSERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_XTRNTRANSDLL", Py_BuildValue("i", YDB_ERR_XTRNTRANSDLL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_XTRNRETVAL", Py_BuildValue("i", YDB_ERR_XTRNRETVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_XTRNRETSTR", Py_BuildValue("i", YDB_ERR_XTRNRETSTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVECODEVAL", Py_BuildValue("i", YDB_ERR_INVECODEVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETECODE", Py_BuildValue("i", YDB_ERR_SETECODE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVSTACODE", Py_BuildValue("i", YDB_ERR_INVSTACODE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPEATERROR", Py_BuildValue("i", YDB_ERR_REPEATERROR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOCANONICNAME", Py_BuildValue("i", YDB_ERR_NOCANONICNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOSUBSCRIPT", Py_BuildValue("i", YDB_ERR_NOSUBSCRIPT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SYSTEMVALUE", Py_BuildValue("i", YDB_ERR_SYSTEMVALUE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIZENOTVALID4", Py_BuildValue("i", YDB_ERR_SIZENOTVALID4));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STRNOTVALID", Py_BuildValue("i", YDB_ERR_STRNOTVALID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CREDNOTPASSED", Py_BuildValue("i", YDB_ERR_CREDNOTPASSED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ERRWETRAP", Py_BuildValue("i", YDB_ERR_ERRWETRAP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRACINGON", Py_BuildValue("i", YDB_ERR_TRACINGON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CITABENV", Py_BuildValue("i", YDB_ERR_CITABENV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CITABOPN", Py_BuildValue("i", YDB_ERR_CITABOPN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CIENTNAME", Py_BuildValue("i", YDB_ERR_CIENTNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CIRTNTYP", Py_BuildValue("i", YDB_ERR_CIRTNTYP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CIRCALLNAME", Py_BuildValue("i", YDB_ERR_CIRCALLNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CIRPARMNAME", Py_BuildValue("i", YDB_ERR_CIRPARMNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CIDIRECTIVE", Py_BuildValue("i", YDB_ERR_CIDIRECTIVE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CIPARTYPE", Py_BuildValue("i", YDB_ERR_CIPARTYPE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CIUNTYPE", Py_BuildValue("i", YDB_ERR_CIUNTYPE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CINOENTRY", Py_BuildValue("i", YDB_ERR_CINOENTRY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLINVSWITCHLMT", Py_BuildValue("i", YDB_ERR_JNLINVSWITCHLMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETZDIR", Py_BuildValue("i", YDB_ERR_SETZDIR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBACTREF", Py_BuildValue("i", YDB_ERR_JOBACTREF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ECLOSTMID", Py_BuildValue("i", YDB_ERR_ECLOSTMID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZFF2MANY", Py_BuildValue("i", YDB_ERR_ZFF2MANY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFSYNCLSTCK", Py_BuildValue("i", YDB_ERR_JNLFSYNCLSTCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DELIMWIDTH", Py_BuildValue("i", YDB_ERR_DELIMWIDTH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBMLCORRUPT", Py_BuildValue("i", YDB_ERR_DBBMLCORRUPT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLCKAVOIDANCE", Py_BuildValue("i", YDB_ERR_DLCKAVOIDANCE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WRITERSTUCK", Py_BuildValue("i", YDB_ERR_WRITERSTUCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PATNOTFOUND", Py_BuildValue("i", YDB_ERR_PATNOTFOUND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVZDIRFORM", Py_BuildValue("i", YDB_ERR_INVZDIRFORM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZDIROUTOFSYNC", Py_BuildValue("i", YDB_ERR_ZDIROUTOFSYNC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GBLNOEXIST", Py_BuildValue("i", YDB_ERR_GBLNOEXIST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXBTLEVEL", Py_BuildValue("i", YDB_ERR_MAXBTLEVEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVMNEMCSPC", Py_BuildValue("i", YDB_ERR_INVMNEMCSPC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLALIGNSZCHG", Py_BuildValue("i", YDB_ERR_JNLALIGNSZCHG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SEFCTNEEDSFULLB", Py_BuildValue("i", YDB_ERR_SEFCTNEEDSFULLB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVFAILCORE", Py_BuildValue("i", YDB_ERR_GVFAILCORE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCDBNOCERTIFY", Py_BuildValue("i", YDB_ERR_DBCDBNOCERTIFY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFRZRESETSUC", Py_BuildValue("i", YDB_ERR_DBFRZRESETSUC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFILEXTERR", Py_BuildValue("i", YDB_ERR_JNLFILEXTERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBEXAMDONE", Py_BuildValue("i", YDB_ERR_JOBEXAMDONE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBEXAMFAIL", Py_BuildValue("i", YDB_ERR_JOBEXAMFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBINTRRQST", Py_BuildValue("i", YDB_ERR_JOBINTRRQST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ERRWZINTR", Py_BuildValue("i", YDB_ERR_ERRWZINTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CLIERR", Py_BuildValue("i", YDB_ERR_CLIERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLNOBEFORE", Py_BuildValue("i", YDB_ERR_REPLNOBEFORE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLJNLCNFLCT", Py_BuildValue("i", YDB_ERR_REPLJNLCNFLCT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLDISABLE", Py_BuildValue("i", YDB_ERR_JNLDISABLE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILEEXISTS", Py_BuildValue("i", YDB_ERR_FILEEXISTS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSTATE", Py_BuildValue("i", YDB_ERR_JNLSTATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLSTATE", Py_BuildValue("i", YDB_ERR_REPLSTATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLCREATE", Py_BuildValue("i", YDB_ERR_JNLCREATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLNOCREATE", Py_BuildValue("i", YDB_ERR_JNLNOCREATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFNF", Py_BuildValue("i", YDB_ERR_JNLFNF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PREVJNLLINKCUT", Py_BuildValue("i", YDB_ERR_PREVJNLLINKCUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PREVJNLLINKSET", Py_BuildValue("i", YDB_ERR_PREVJNLLINKSET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILENAMETOOLONG", Py_BuildValue("i", YDB_ERR_FILENAMETOOLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REQRECOV", Py_BuildValue("i", YDB_ERR_REQRECOV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLTRANS2BIG", Py_BuildValue("i", YDB_ERR_JNLTRANS2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSWITCHTOOSM", Py_BuildValue("i", YDB_ERR_JNLSWITCHTOOSM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSWITCHSZCHG", Py_BuildValue("i", YDB_ERR_JNLSWITCHSZCHG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTRNDMACC", Py_BuildValue("i", YDB_ERR_NOTRNDMACC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TMPFILENOCRE", Py_BuildValue("i", YDB_ERR_TMPFILENOCRE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SHRMEMEXHAUSTED", Py_BuildValue("i", YDB_ERR_SHRMEMEXHAUSTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSENDOPER", Py_BuildValue("i", YDB_ERR_JNLSENDOPER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPSUBSNUL", Py_BuildValue("i", YDB_ERR_DDPSUBSNUL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPNOCONNECT", Py_BuildValue("i", YDB_ERR_DDPNOCONNECT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPCONGEST", Py_BuildValue("i", YDB_ERR_DDPCONGEST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPSHUTDOWN", Py_BuildValue("i", YDB_ERR_DDPSHUTDOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPTOOMANYPROCS", Py_BuildValue("i", YDB_ERR_DDPTOOMANYPROCS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPBADRESPONSE", Py_BuildValue("i", YDB_ERR_DDPBADRESPONSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPINVCKT", Py_BuildValue("i", YDB_ERR_DDPINVCKT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPVOLSETCONFIG", Py_BuildValue("i", YDB_ERR_DDPVOLSETCONFIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPCONFGOOD", Py_BuildValue("i", YDB_ERR_DDPCONFGOOD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPCONFIGNORE", Py_BuildValue("i", YDB_ERR_DDPCONFIGNORE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPCONFINCOMPL", Py_BuildValue("i", YDB_ERR_DDPCONFINCOMPL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPCONFBADVOL", Py_BuildValue("i", YDB_ERR_DDPCONFBADVOL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPCONFBADUCI", Py_BuildValue("i", YDB_ERR_DDPCONFBADUCI));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPCONFBADGLD", Py_BuildValue("i", YDB_ERR_DDPCONFBADGLD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPRECSIZNOTNUM", Py_BuildValue("i", YDB_ERR_DDPRECSIZNOTNUM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPOUTMSG2BIG", Py_BuildValue("i", YDB_ERR_DDPOUTMSG2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPNOSERVER", Py_BuildValue("i", YDB_ERR_DDPNOSERVER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTEXRELEASED", Py_BuildValue("i", YDB_ERR_MUTEXRELEASED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLCRESTATUS", Py_BuildValue("i", YDB_ERR_JNLCRESTATUS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZBREAKFAIL", Py_BuildValue("i", YDB_ERR_ZBREAKFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLLVERSION", Py_BuildValue("i", YDB_ERR_DLLVERSION));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVZROENT", Py_BuildValue("i", YDB_ERR_INVZROENT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DDPLOGERR", Py_BuildValue("i", YDB_ERR_DDPLOGERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GETSOCKNAMERR", Py_BuildValue("i", YDB_ERR_GETSOCKNAMERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVYDBEXIT", Py_BuildValue("i", YDB_ERR_INVYDBEXIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CIMAXPARAM", Py_BuildValue("i", YDB_ERR_CIMAXPARAM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1171", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1171));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CIMAXLEVELS", Py_BuildValue("i", YDB_ERR_CIMAXLEVELS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBINTRRETHROW", Py_BuildValue("i", YDB_ERR_JOBINTRRETHROW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STARFILE", Py_BuildValue("i", YDB_ERR_STARFILE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOSTARFILE", Py_BuildValue("i", YDB_ERR_NOSTARFILE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUJNLSTAT", Py_BuildValue("i", YDB_ERR_MUJNLSTAT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLTPNEST", Py_BuildValue("i", YDB_ERR_JNLTPNEST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLOFFJNLON", Py_BuildValue("i", YDB_ERR_REPLOFFJNLON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILEDELFAIL", Py_BuildValue("i", YDB_ERR_FILEDELFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVQUALTIME", Py_BuildValue("i", YDB_ERR_INVQUALTIME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTPOSITIVE", Py_BuildValue("i", YDB_ERR_NOTPOSITIVE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVREDIRQUAL", Py_BuildValue("i", YDB_ERR_INVREDIRQUAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVERRORLIM", Py_BuildValue("i", YDB_ERR_INVERRORLIM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVIDQUAL", Py_BuildValue("i", YDB_ERR_INVIDQUAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVTRNSQUAL", Py_BuildValue("i", YDB_ERR_INVTRNSQUAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLNOBIJBACK", Py_BuildValue("i", YDB_ERR_JNLNOBIJBACK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETREG2RESYNC", Py_BuildValue("i", YDB_ERR_SETREG2RESYNC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLALIGNTOOSM", Py_BuildValue("i", YDB_ERR_JNLALIGNTOOSM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFILEOPNERR", Py_BuildValue("i", YDB_ERR_JNLFILEOPNERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFILECLOSERR", Py_BuildValue("i", YDB_ERR_JNLFILECLOSERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLSTATEOFF", Py_BuildValue("i", YDB_ERR_REPLSTATEOFF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUJNLPREVGEN", Py_BuildValue("i", YDB_ERR_MUJNLPREVGEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUPJNLINTERRUPT", Py_BuildValue("i", YDB_ERR_MUPJNLINTERRUPT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ROLLBKINTERRUPT", Py_BuildValue("i", YDB_ERR_ROLLBKINTERRUPT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RLBKJNSEQ", Py_BuildValue("i", YDB_ERR_RLBKJNSEQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLRECFMT", Py_BuildValue("i", YDB_ERR_REPLRECFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PRIMARYNOTROOT", Py_BuildValue("i", YDB_ERR_PRIMARYNOTROOT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFRZRESETFL", Py_BuildValue("i", YDB_ERR_DBFRZRESETFL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLCYCLE", Py_BuildValue("i", YDB_ERR_JNLCYCLE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLPREVRECOV", Py_BuildValue("i", YDB_ERR_JNLPREVRECOV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RESOLVESEQNO", Py_BuildValue("i", YDB_ERR_RESOLVESEQNO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BOVTNGTEOVTN", Py_BuildValue("i", YDB_ERR_BOVTNGTEOVTN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BOVTMGTEOVTM", Py_BuildValue("i", YDB_ERR_BOVTMGTEOVTM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BEGSEQGTENDSEQ", Py_BuildValue("i", YDB_ERR_BEGSEQGTENDSEQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBADDRALIGN", Py_BuildValue("i", YDB_ERR_DBADDRALIGN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBWCVERIFYSTART", Py_BuildValue("i", YDB_ERR_DBWCVERIFYSTART));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBWCVERIFYEND", Py_BuildValue("i", YDB_ERR_DBWCVERIFYEND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUPIPSIG", Py_BuildValue("i", YDB_ERR_MUPIPSIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_HTSHRINKFAIL", Py_BuildValue("i", YDB_ERR_HTSHRINKFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STPEXPFAIL", Py_BuildValue("i", YDB_ERR_STPEXPFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBTUWRNG", Py_BuildValue("i", YDB_ERR_DBBTUWRNG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBTUFIXED", Py_BuildValue("i", YDB_ERR_DBBTUFIXED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMAXREC2BIG", Py_BuildValue("i", YDB_ERR_DBMAXREC2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCSCNNOTCMPLT", Py_BuildValue("i", YDB_ERR_DBCSCNNOTCMPLT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCBADFILE", Py_BuildValue("i", YDB_ERR_DBCBADFILE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCNOEXTND", Py_BuildValue("i", YDB_ERR_DBCNOEXTND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCINTEGERR", Py_BuildValue("i", YDB_ERR_DBCINTEGERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMINRESBYTES", Py_BuildValue("i", YDB_ERR_DBMINRESBYTES));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCNOTSAMEDB", Py_BuildValue("i", YDB_ERR_DBCNOTSAMEDB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCDBCERTIFIED", Py_BuildValue("i", YDB_ERR_DBCDBCERTIFIED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCMODBLK2BIG", Py_BuildValue("i", YDB_ERR_DBCMODBLK2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCREC2BIG", Py_BuildValue("i", YDB_ERR_DBCREC2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCCMDFAIL", Py_BuildValue("i", YDB_ERR_DBCCMDFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCKILLIP", Py_BuildValue("i", YDB_ERR_DBCKILLIP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCNOFINISH", Py_BuildValue("i", YDB_ERR_DBCNOFINISH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DYNUPGRDFAIL", Py_BuildValue("i", YDB_ERR_DYNUPGRDFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MMNODYNDWNGRD", Py_BuildValue("i", YDB_ERR_MMNODYNDWNGRD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MMNODYNUPGRD", Py_BuildValue("i", YDB_ERR_MMNODYNUPGRD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUDWNGRDNRDY", Py_BuildValue("i", YDB_ERR_MUDWNGRDNRDY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUDWNGRDTN", Py_BuildValue("i", YDB_ERR_MUDWNGRDTN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUDWNGRDNOTPOS", Py_BuildValue("i", YDB_ERR_MUDWNGRDNOTPOS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUUPGRDNRDY", Py_BuildValue("i", YDB_ERR_MUUPGRDNRDY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TNWARN", Py_BuildValue("i", YDB_ERR_TNWARN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TNTOOLARGE", Py_BuildValue("i", YDB_ERR_TNTOOLARGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SHMPLRECOV", Py_BuildValue("i", YDB_ERR_SHMPLRECOV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUNOSTRMBKUP", Py_BuildValue("i", YDB_ERR_MUNOSTRMBKUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EPOCHTNHI", Py_BuildValue("i", YDB_ERR_EPOCHTNHI));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CHNGTPRSLVTM", Py_BuildValue("i", YDB_ERR_CHNGTPRSLVTM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLUNXPCTERR", Py_BuildValue("i", YDB_ERR_JNLUNXPCTERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OMISERVHANG", Py_BuildValue("i", YDB_ERR_OMISERVHANG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RSVDBYTE2HIGH", Py_BuildValue("i", YDB_ERR_RSVDBYTE2HIGH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BKUPTMPFILOPEN", Py_BuildValue("i", YDB_ERR_BKUPTMPFILOPEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BKUPTMPFILWRITE", Py_BuildValue("i", YDB_ERR_BKUPTMPFILWRITE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1244", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1244));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADBGSZ2", Py_BuildValue("i", YDB_ERR_LOADBGSZ2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADEDSZ2", Py_BuildValue("i", YDB_ERR_LOADEDSZ2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTMISMTCH", Py_BuildValue("i", YDB_ERR_REPLINSTMISMTCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTREAD", Py_BuildValue("i", YDB_ERR_REPLINSTREAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTDBMATCH", Py_BuildValue("i", YDB_ERR_REPLINSTDBMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTNMSAME", Py_BuildValue("i", YDB_ERR_REPLINSTNMSAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTNMUNDEF", Py_BuildValue("i", YDB_ERR_REPLINSTNMUNDEF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTNMLEN", Py_BuildValue("i", YDB_ERR_REPLINSTNMLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTNOHIST", Py_BuildValue("i", YDB_ERR_REPLINSTNOHIST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTSECLEN", Py_BuildValue("i", YDB_ERR_REPLINSTSECLEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTSECMTCH", Py_BuildValue("i", YDB_ERR_REPLINSTSECMTCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTSECNONE", Py_BuildValue("i", YDB_ERR_REPLINSTSECNONE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTSECUNDF", Py_BuildValue("i", YDB_ERR_REPLINSTSECUNDF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTSEQORD", Py_BuildValue("i", YDB_ERR_REPLINSTSEQORD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTSTNDALN", Py_BuildValue("i", YDB_ERR_REPLINSTSTNDALN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLREQROLLBACK", Py_BuildValue("i", YDB_ERR_REPLREQROLLBACK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REQROLLBACK", Py_BuildValue("i", YDB_ERR_REQROLLBACK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVOBJFILE", Py_BuildValue("i", YDB_ERR_INVOBJFILE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRCSRVEXISTS", Py_BuildValue("i", YDB_ERR_SRCSRVEXISTS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRCSRVNOTEXIST", Py_BuildValue("i", YDB_ERR_SRCSRVNOTEXIST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRCSRVTOOMANY", Py_BuildValue("i", YDB_ERR_SRCSRVTOOMANY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLPOOLBADSLOT", Py_BuildValue("i", YDB_ERR_JNLPOOLBADSLOT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOENDIANCVT", Py_BuildValue("i", YDB_ERR_NOENDIANCVT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ENDIANCVT", Py_BuildValue("i", YDB_ERR_ENDIANCVT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBENDIAN", Py_BuildValue("i", YDB_ERR_DBENDIAN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADCHSET", Py_BuildValue("i", YDB_ERR_BADCHSET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADCASECODE", Py_BuildValue("i", YDB_ERR_BADCASECODE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADCHAR", Py_BuildValue("i", YDB_ERR_BADCHAR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLRCILLEGAL", Py_BuildValue("i", YDB_ERR_DLRCILLEGAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NONUTF8LOCALE", Py_BuildValue("i", YDB_ERR_NONUTF8LOCALE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVDLRCVAL", Py_BuildValue("i", YDB_ERR_INVDLRCVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMISALIGN", Py_BuildValue("i", YDB_ERR_DBMISALIGN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADINVCHSET", Py_BuildValue("i", YDB_ERR_LOADINVCHSET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLLCHSETM", Py_BuildValue("i", YDB_ERR_DLLCHSETM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DLLCHSETUTF8", Py_BuildValue("i", YDB_ERR_DLLCHSETUTF8));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BOMMISMATCH", Py_BuildValue("i", YDB_ERR_BOMMISMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WIDTHTOOSMALL", Py_BuildValue("i", YDB_ERR_WIDTHTOOSMALL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKMAX", Py_BuildValue("i", YDB_ERR_SOCKMAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PADCHARINVALID", Py_BuildValue("i", YDB_ERR_PADCHARINVALID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCNOPREALLOUTPAR", Py_BuildValue("i", YDB_ERR_ZCNOPREALLOUTPAR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SVNEXPECTED", Py_BuildValue("i", YDB_ERR_SVNEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SVNONEW", Py_BuildValue("i", YDB_ERR_SVNONEW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZINTDIRECT", Py_BuildValue("i", YDB_ERR_ZINTDIRECT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZINTRECURSEIO", Py_BuildValue("i", YDB_ERR_ZINTRECURSEIO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MRTMAXEXCEEDED", Py_BuildValue("i", YDB_ERR_MRTMAXEXCEEDED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLCLOSED", Py_BuildValue("i", YDB_ERR_JNLCLOSED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RLBKNOBIMG", Py_BuildValue("i", YDB_ERR_RLBKNOBIMG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RLBKJNLNOBIMG", Py_BuildValue("i", YDB_ERR_RLBKJNLNOBIMG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RLBKLOSTTNONLY", Py_BuildValue("i", YDB_ERR_RLBKLOSTTNONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_KILLBYSIGSINFO3", Py_BuildValue("i", YDB_ERR_KILLBYSIGSINFO3));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRTMPPATH", Py_BuildValue("i", YDB_ERR_GTMSECSHRTMPPATH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1296", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1296));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVMEMRESRV", Py_BuildValue("i", YDB_ERR_INVMEMRESRV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OPCOMMISSED", Py_BuildValue("i", YDB_ERR_OPCOMMISSED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COMMITWAITSTUCK", Py_BuildValue("i", YDB_ERR_COMMITWAITSTUCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COMMITWAITPID", Py_BuildValue("i", YDB_ERR_COMMITWAITPID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UPDREPLSTATEOFF", Py_BuildValue("i", YDB_ERR_UPDREPLSTATEOFF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LITNONGRAPH", Py_BuildValue("i", YDB_ERR_LITNONGRAPH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFHEADERR8", Py_BuildValue("i", YDB_ERR_DBFHEADERR8));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MMBEFOREJNL", Py_BuildValue("i", YDB_ERR_MMBEFOREJNL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MMNOBFORRPL", Py_BuildValue("i", YDB_ERR_MMNOBFORRPL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_KILLABANDONED", Py_BuildValue("i", YDB_ERR_KILLABANDONED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BACKUPKILLIP", Py_BuildValue("i", YDB_ERR_BACKUPKILLIP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOGTOOLONG", Py_BuildValue("i", YDB_ERR_LOGTOOLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOALIASLIST", Py_BuildValue("i", YDB_ERR_NOALIASLIST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ALIASEXPECTED", Py_BuildValue("i", YDB_ERR_ALIASEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VIEWLVN", Py_BuildValue("i", YDB_ERR_VIEWLVN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DZWRNOPAREN", Py_BuildValue("i", YDB_ERR_DZWRNOPAREN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DZWRNOALIAS", Py_BuildValue("i", YDB_ERR_DZWRNOALIAS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FREEZEERR", Py_BuildValue("i", YDB_ERR_FREEZEERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CLOSEFAIL", Py_BuildValue("i", YDB_ERR_CLOSEFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTINIT", Py_BuildValue("i", YDB_ERR_CRYPTINIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTOPFAILED", Py_BuildValue("i", YDB_ERR_CRYPTOPFAILED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTDLNOOPEN", Py_BuildValue("i", YDB_ERR_CRYPTDLNOOPEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTNOV4", Py_BuildValue("i", YDB_ERR_CRYPTNOV4));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTNOMM", Py_BuildValue("i", YDB_ERR_CRYPTNOMM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_READONLYNOBG", Py_BuildValue("i", YDB_ERR_READONLYNOBG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTKEYFETCHFAILED", Py_BuildValue("i", YDB_ERR_CRYPTKEYFETCHFAILED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTKEYFETCHFAILEDNF", Py_BuildValue("i", YDB_ERR_CRYPTKEYFETCHFAILEDNF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTHASHGENFAILED", Py_BuildValue("i", YDB_ERR_CRYPTHASHGENFAILED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTNOKEY", Py_BuildValue("i", YDB_ERR_CRYPTNOKEY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADTAG", Py_BuildValue("i", YDB_ERR_BADTAG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ICUVERLT36", Py_BuildValue("i", YDB_ERR_ICUVERLT36));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ICUSYMNOTFOUND", Py_BuildValue("i", YDB_ERR_ICUSYMNOTFOUND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STUCKACT", Py_BuildValue("i", YDB_ERR_STUCKACT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CALLINAFTERXIT", Py_BuildValue("i", YDB_ERR_CALLINAFTERXIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOCKSPACEFULL", Py_BuildValue("i", YDB_ERR_LOCKSPACEFULL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_IOERROR", Py_BuildValue("i", YDB_ERR_IOERROR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXSSREACHED", Py_BuildValue("i", YDB_ERR_MAXSSREACHED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SNAPSHOTNOV4", Py_BuildValue("i", YDB_ERR_SNAPSHOTNOV4));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SSV4NOALLOW", Py_BuildValue("i", YDB_ERR_SSV4NOALLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SSTMPDIRSTAT", Py_BuildValue("i", YDB_ERR_SSTMPDIRSTAT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SSTMPCREATE", Py_BuildValue("i", YDB_ERR_SSTMPCREATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFILEDUP", Py_BuildValue("i", YDB_ERR_JNLFILEDUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SSPREMATEOF", Py_BuildValue("i", YDB_ERR_SSPREMATEOF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SSFILOPERR", Py_BuildValue("i", YDB_ERR_SSFILOPERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REGSSFAIL", Py_BuildValue("i", YDB_ERR_REGSSFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SSSHMCLNUPFAIL", Py_BuildValue("i", YDB_ERR_SSSHMCLNUPFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SSFILCLNUPFAIL", Py_BuildValue("i", YDB_ERR_SSFILCLNUPFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETINTRIGONLY", Py_BuildValue("i", YDB_ERR_SETINTRIGONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXTRIGNEST", Py_BuildValue("i", YDB_ERR_MAXTRIGNEST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGCOMPFAIL", Py_BuildValue("i", YDB_ERR_TRIGCOMPFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOZTRAPINTRIG", Py_BuildValue("i", YDB_ERR_NOZTRAPINTRIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZTWORMHOLE2BIG", Py_BuildValue("i", YDB_ERR_ZTWORMHOLE2BIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLENDIANLITTLE", Py_BuildValue("i", YDB_ERR_JNLENDIANLITTLE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLENDIANBIG", Py_BuildValue("i", YDB_ERR_JNLENDIANBIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGINVCHSET", Py_BuildValue("i", YDB_ERR_TRIGINVCHSET));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGREPLSTATE", Py_BuildValue("i", YDB_ERR_TRIGREPLSTATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVDATAGETFAIL", Py_BuildValue("i", YDB_ERR_GVDATAGETFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIG2NOTRIG", Py_BuildValue("i", YDB_ERR_TRIG2NOTRIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZGOTOINVLVL", Py_BuildValue("i", YDB_ERR_ZGOTOINVLVL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGTCOMMIT", Py_BuildValue("i", YDB_ERR_TRIGTCOMMIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGTLVLCHNG", Py_BuildValue("i", YDB_ERR_TRIGTLVLCHNG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGNAMEUNIQ", Py_BuildValue("i", YDB_ERR_TRIGNAMEUNIQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZTRIGINVACT", Py_BuildValue("i", YDB_ERR_ZTRIGINVACT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INDRCOMPFAIL", Py_BuildValue("i", YDB_ERR_INDRCOMPFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_QUITALSINV", Py_BuildValue("i", YDB_ERR_QUITALSINV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PROCTERM", Py_BuildValue("i", YDB_ERR_PROCTERM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRCLNNTDSP", Py_BuildValue("i", YDB_ERR_SRCLNNTDSP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ARROWNTDSP", Py_BuildValue("i", YDB_ERR_ARROWNTDSP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGDEFBAD", Py_BuildValue("i", YDB_ERR_TRIGDEFBAD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGSUBSCRANGE", Py_BuildValue("i", YDB_ERR_TRIGSUBSCRANGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGDATAIGNORE", Py_BuildValue("i", YDB_ERR_TRIGDATAIGNORE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGIS", Py_BuildValue("i", YDB_ERR_TRIGIS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TCOMMITDISALLOW", Py_BuildValue("i", YDB_ERR_TCOMMITDISALLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SSATTACHSHM", Py_BuildValue("i", YDB_ERR_SSATTACHSHM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGDEFNOSYNC", Py_BuildValue("i", YDB_ERR_TRIGDEFNOSYNC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRESTMAX", Py_BuildValue("i", YDB_ERR_TRESTMAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZLINKBYPASS", Py_BuildValue("i", YDB_ERR_ZLINKBYPASS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GBLEXPECTED", Py_BuildValue("i", YDB_ERR_GBLEXPECTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GVZTRIGFAIL", Py_BuildValue("i", YDB_ERR_GVZTRIGFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUUSERLBK", Py_BuildValue("i", YDB_ERR_MUUSERLBK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETINSETTRIGONLY", Py_BuildValue("i", YDB_ERR_SETINSETTRIGONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DZTRIGINTRIG", Py_BuildValue("i", YDB_ERR_DZTRIGINTRIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LSINSERTED", Py_BuildValue("i", YDB_ERR_LSINSERTED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BOOLSIDEFFECT", Py_BuildValue("i", YDB_ERR_BOOLSIDEFFECT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBADUPGRDSTATE", Py_BuildValue("i", YDB_ERR_DBBADUPGRDSTATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WRITEWAITPID", Py_BuildValue("i", YDB_ERR_WRITEWAITPID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZGOCALLOUTIN", Py_BuildValue("i", YDB_ERR_ZGOCALLOUTIN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1384", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1384));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLXENDIANFAIL", Py_BuildValue("i", YDB_ERR_REPLXENDIANFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1386", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1386));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRCHDIRF", Py_BuildValue("i", YDB_ERR_GTMSECSHRCHDIRF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLORDBFLU", Py_BuildValue("i", YDB_ERR_JNLORDBFLU));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCCLNUPRTNMISNG", Py_BuildValue("i", YDB_ERR_ZCCLNUPRTNMISNG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZCINVALIDKEYWORD", Py_BuildValue("i", YDB_ERR_ZCINVALIDKEYWORD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLMULTINSTUPDATE", Py_BuildValue("i", YDB_ERR_REPLMULTINSTUPDATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBSHMNAMEDIFF", Py_BuildValue("i", YDB_ERR_DBSHMNAMEDIFF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SHMREMOVED", Py_BuildValue("i", YDB_ERR_SHMREMOVED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVICEWRITEONLY", Py_BuildValue("i", YDB_ERR_DEVICEWRITEONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ICUERROR", Py_BuildValue("i", YDB_ERR_ICUERROR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZDATEBADDATE", Py_BuildValue("i", YDB_ERR_ZDATEBADDATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZDATEBADTIME", Py_BuildValue("i", YDB_ERR_ZDATEBADTIME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COREINPROGRESS", Py_BuildValue("i", YDB_ERR_COREINPROGRESS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MAXSEMGETRETRY", Py_BuildValue("i", YDB_ERR_MAXSEMGETRETRY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLNOREPL", Py_BuildValue("i", YDB_ERR_JNLNOREPL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLRECINCMPL", Py_BuildValue("i", YDB_ERR_JNLRECINCMPL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLALLOCGROW", Py_BuildValue("i", YDB_ERR_JNLALLOCGROW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVTRCGRP", Py_BuildValue("i", YDB_ERR_INVTRCGRP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUINFOUINT6", Py_BuildValue("i", YDB_ERR_MUINFOUINT6));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOLOCKMATCH", Py_BuildValue("i", YDB_ERR_NOLOCKMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADREGION", Py_BuildValue("i", YDB_ERR_BADREGION));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOCKSPACEUSE", Py_BuildValue("i", YDB_ERR_LOCKSPACEUSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JIUNHNDINT", Py_BuildValue("i", YDB_ERR_JIUNHNDINT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMASSERT2", Py_BuildValue("i", YDB_ERR_GTMASSERT2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZTRIGNOTRW", Py_BuildValue("i", YDB_ERR_ZTRIGNOTRW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGMODREGNOTRW", Py_BuildValue("i", YDB_ERR_TRIGMODREGNOTRW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INSNOTJOINED", Py_BuildValue("i", YDB_ERR_INSNOTJOINED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INSROLECHANGE", Py_BuildValue("i", YDB_ERR_INSROLECHANGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INSUNKNOWN", Py_BuildValue("i", YDB_ERR_INSUNKNOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NORESYNCSUPPLONLY", Py_BuildValue("i", YDB_ERR_NORESYNCSUPPLONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NORESYNCUPDATERONLY", Py_BuildValue("i", YDB_ERR_NORESYNCUPDATERONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOSUPPLSUPPL", Py_BuildValue("i", YDB_ERR_NOSUPPLSUPPL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPL2OLD", Py_BuildValue("i", YDB_ERR_REPL2OLD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTRFILEXISTS", Py_BuildValue("i", YDB_ERR_EXTRFILEXISTS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUUSERECOV", Py_BuildValue("i", YDB_ERR_MUUSERECOV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SECNOTSUPPLEMENTARY", Py_BuildValue("i", YDB_ERR_SECNOTSUPPLEMENTARY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SUPRCVRNEEDSSUPSRC", Py_BuildValue("i", YDB_ERR_SUPRCVRNEEDSSUPSRC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PEERPIDMISMATCH", Py_BuildValue("i", YDB_ERR_PEERPIDMISMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETITIMERFAILED", Py_BuildValue("i", YDB_ERR_SETITIMERFAILED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UPDSYNC2MTINS", Py_BuildValue("i", YDB_ERR_UPDSYNC2MTINS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UPDSYNCINSTFILE", Py_BuildValue("i", YDB_ERR_UPDSYNCINSTFILE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REUSEINSTNAME", Py_BuildValue("i", YDB_ERR_REUSEINSTNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RCVRMANYSTRMS", Py_BuildValue("i", YDB_ERR_RCVRMANYSTRMS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RSYNCSTRMVAL", Py_BuildValue("i", YDB_ERR_RSYNCSTRMVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RLBKSTRMSEQ", Py_BuildValue("i", YDB_ERR_RLBKSTRMSEQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RESOLVESEQSTRM", Py_BuildValue("i", YDB_ERR_RESOLVESEQSTRM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTDBSTRM", Py_BuildValue("i", YDB_ERR_REPLINSTDBSTRM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RESUMESTRMNUM", Py_BuildValue("i", YDB_ERR_RESUMESTRMNUM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKSTART", Py_BuildValue("i", YDB_ERR_ORLBKSTART));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKTERMNTD", Py_BuildValue("i", YDB_ERR_ORLBKTERMNTD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKCMPLT", Py_BuildValue("i", YDB_ERR_ORLBKCMPLT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKNOSTP", Py_BuildValue("i", YDB_ERR_ORLBKNOSTP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKFRZPROG", Py_BuildValue("i", YDB_ERR_ORLBKFRZPROG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKFRZOVER", Py_BuildValue("i", YDB_ERR_ORLBKFRZOVER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKNOV4BLK", Py_BuildValue("i", YDB_ERR_ORLBKNOV4BLK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBROLLEDBACK", Py_BuildValue("i", YDB_ERR_DBROLLEDBACK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSEWCREINIT", Py_BuildValue("i", YDB_ERR_DSEWCREINIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MURNDWNOVRD", Py_BuildValue("i", YDB_ERR_MURNDWNOVRD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLONLNRLBK", Py_BuildValue("i", YDB_ERR_REPLONLNRLBK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SRVLCKWT2LNG", Py_BuildValue("i", YDB_ERR_SRVLCKWT2LNG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_IGNBMPMRKFREE", Py_BuildValue("i", YDB_ERR_IGNBMPMRKFREE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PERMGENFAIL", Py_BuildValue("i", YDB_ERR_PERMGENFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PERMGENDIAG", Py_BuildValue("i", YDB_ERR_PERMGENDIAG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNC1ATIME", Py_BuildValue("i", YDB_ERR_MUTRUNC1ATIME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNCBACKINPROG", Py_BuildValue("i", YDB_ERR_MUTRUNCBACKINPROG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNCERROR", Py_BuildValue("i", YDB_ERR_MUTRUNCERROR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNCFAIL", Py_BuildValue("i", YDB_ERR_MUTRUNCFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNCNOSPACE", Py_BuildValue("i", YDB_ERR_MUTRUNCNOSPACE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNCNOTBG", Py_BuildValue("i", YDB_ERR_MUTRUNCNOTBG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNCNOV4", Py_BuildValue("i", YDB_ERR_MUTRUNCNOV4));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNCPERCENT", Py_BuildValue("i", YDB_ERR_MUTRUNCPERCENT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNCSSINPROG", Py_BuildValue("i", YDB_ERR_MUTRUNCSSINPROG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUTRUNCSUCCESS", Py_BuildValue("i", YDB_ERR_MUTRUNCSUCCESS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RSYNCSTRMSUPPLONLY", Py_BuildValue("i", YDB_ERR_RSYNCSTRMSUPPLONLY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STRMNUMIS", Py_BuildValue("i", YDB_ERR_STRMNUMIS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STRMNUMMISMTCH1", Py_BuildValue("i", YDB_ERR_STRMNUMMISMTCH1));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STRMNUMMISMTCH2", Py_BuildValue("i", YDB_ERR_STRMNUMMISMTCH2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STRMSEQMISMTCH", Py_BuildValue("i", YDB_ERR_STRMSEQMISMTCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOCKSPACEINFO", Py_BuildValue("i", YDB_ERR_LOCKSPACEINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JRTNULLFAIL", Py_BuildValue("i", YDB_ERR_JRTNULLFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOCKSUB2LONG", Py_BuildValue("i", YDB_ERR_LOCKSUB2LONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RESRCWAIT", Py_BuildValue("i", YDB_ERR_RESRCWAIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RESRCINTRLCKBYPAS", Py_BuildValue("i", YDB_ERR_RESRCINTRLCKBYPAS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFHEADERRANY", Py_BuildValue("i", YDB_ERR_DBFHEADERRANY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTFROZEN", Py_BuildValue("i", YDB_ERR_REPLINSTFROZEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTFREEZECOMMENT", Py_BuildValue("i", YDB_ERR_REPLINSTFREEZECOMMENT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTUNFROZEN", Py_BuildValue("i", YDB_ERR_REPLINSTUNFROZEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSKNOSPCAVAIL", Py_BuildValue("i", YDB_ERR_DSKNOSPCAVAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSKNOSPCBLOCKED", Py_BuildValue("i", YDB_ERR_DSKNOSPCBLOCKED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSKSPCAVAILABLE", Py_BuildValue("i", YDB_ERR_DSKSPCAVAILABLE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ENOSPCQIODEFER", Py_BuildValue("i", YDB_ERR_ENOSPCQIODEFER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CUSTOMFILOPERR", Py_BuildValue("i", YDB_ERR_CUSTOMFILOPERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CUSTERRNOTFND", Py_BuildValue("i", YDB_ERR_CUSTERRNOTFND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CUSTERRSYNTAX", Py_BuildValue("i", YDB_ERR_CUSTERRSYNTAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKINPROG", Py_BuildValue("i", YDB_ERR_ORLBKINPROG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBSPANGLOINCMP", Py_BuildValue("i", YDB_ERR_DBSPANGLOINCMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBSPANCHUNKORD", Py_BuildValue("i", YDB_ERR_DBSPANCHUNKORD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBDATAMX", Py_BuildValue("i", YDB_ERR_DBDATAMX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBIOERR", Py_BuildValue("i", YDB_ERR_DBIOERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INITORRESUME", Py_BuildValue("i", YDB_ERR_INITORRESUME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRNOARG0", Py_BuildValue("i", YDB_ERR_GTMSECSHRNOARG0));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRISNOT", Py_BuildValue("i", YDB_ERR_GTMSECSHRISNOT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMSECSHRBADDIR", Py_BuildValue("i", YDB_ERR_GTMSECSHRBADDIR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLBUFFREGUPD", Py_BuildValue("i", YDB_ERR_JNLBUFFREGUPD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLBUFFDBUPD", Py_BuildValue("i", YDB_ERR_JNLBUFFDBUPD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOCKINCR2HIGH", Py_BuildValue("i", YDB_ERR_LOCKINCR2HIGH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOCKIS", Py_BuildValue("i", YDB_ERR_LOCKIS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LDSPANGLOINCMP", Py_BuildValue("i", YDB_ERR_LDSPANGLOINCMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUFILRNDWNFL2", Py_BuildValue("i", YDB_ERR_MUFILRNDWNFL2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUINSTFROZEN", Py_BuildValue("i", YDB_ERR_MUINSTFROZEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUINSTUNFROZEN", Py_BuildValue("i", YDB_ERR_MUINSTUNFROZEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GTMEISDIR", Py_BuildValue("i", YDB_ERR_GTMEISDIR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SPCLZMSG", Py_BuildValue("i", YDB_ERR_SPCLZMSG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUNOTALLINTEG", Py_BuildValue("i", YDB_ERR_MUNOTALLINTEG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BKUPRUNNING", Py_BuildValue("i", YDB_ERR_BKUPRUNNING));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUSIZEINVARG", Py_BuildValue("i", YDB_ERR_MUSIZEINVARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUSIZEFAIL", Py_BuildValue("i", YDB_ERR_MUSIZEFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIDEEFFECTEVAL", Py_BuildValue("i", YDB_ERR_SIDEEFFECTEVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTINIT2", Py_BuildValue("i", YDB_ERR_CRYPTINIT2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTDLNOOPEN2", Py_BuildValue("i", YDB_ERR_CRYPTDLNOOPEN2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTBADCONFIG", Py_BuildValue("i", YDB_ERR_CRYPTBADCONFIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBCOLLREQ", Py_BuildValue("i", YDB_ERR_DBCOLLREQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETEXTRENV", Py_BuildValue("i", YDB_ERR_SETEXTRENV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTALLDBRNDWN", Py_BuildValue("i", YDB_ERR_NOTALLDBRNDWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPRESTNESTERR", Py_BuildValue("i", YDB_ERR_TPRESTNESTERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLFILRDOPN", Py_BuildValue("i", YDB_ERR_JNLFILRDOPN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1514", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1514));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FTOKKEY", Py_BuildValue("i", YDB_ERR_FTOKKEY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SEMID", Py_BuildValue("i", YDB_ERR_SEMID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLQIOSALVAGE", Py_BuildValue("i", YDB_ERR_JNLQIOSALVAGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FAKENOSPCLEARED", Py_BuildValue("i", YDB_ERR_FAKENOSPCLEARED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MMFILETOOLARGE", Py_BuildValue("i", YDB_ERR_MMFILETOOLARGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADZPEEKARG", Py_BuildValue("i", YDB_ERR_BADZPEEKARG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADZPEEKRANGE", Py_BuildValue("i", YDB_ERR_BADZPEEKRANGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BADZPEEKFMT", Py_BuildValue("i", YDB_ERR_BADZPEEKFMT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBMBMINCFREFIXED", Py_BuildValue("i", YDB_ERR_DBMBMINCFREFIXED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NULLENTRYREF", Py_BuildValue("i", YDB_ERR_NULLENTRYREF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZPEEKNORPLINFO", Py_BuildValue("i", YDB_ERR_ZPEEKNORPLINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MMREGNOACCESS", Py_BuildValue("i", YDB_ERR_MMREGNOACCESS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MALLOCMAXUNIX", Py_BuildValue("i", YDB_ERR_MALLOCMAXUNIX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1528", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1528));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_HOSTCONFLICT", Py_BuildValue("i", YDB_ERR_HOSTCONFLICT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GETADDRINFO", Py_BuildValue("i", YDB_ERR_GETADDRINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GETNAMEINFO", Py_BuildValue("i", YDB_ERR_GETNAMEINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKBIND", Py_BuildValue("i", YDB_ERR_SOCKBIND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INSTFRZDEFER", Py_BuildValue("i", YDB_ERR_INSTFRZDEFER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1534", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1534));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REGOPENFAIL", Py_BuildValue("i", YDB_ERR_REGOPENFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLINSTNOSHM", Py_BuildValue("i", YDB_ERR_REPLINSTNOSHM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVPARMTOOSMALL", Py_BuildValue("i", YDB_ERR_DEVPARMTOOSMALL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REMOTEDBNOSPGBL", Py_BuildValue("i", YDB_ERR_REMOTEDBNOSPGBL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NCTCOLLSPGBL", Py_BuildValue("i", YDB_ERR_NCTCOLLSPGBL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ACTCOLLMISMTCH", Py_BuildValue("i", YDB_ERR_ACTCOLLMISMTCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_GBLNOMAPTOREG", Py_BuildValue("i", YDB_ERR_GBLNOMAPTOREG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ISSPANGBL", Py_BuildValue("i", YDB_ERR_ISSPANGBL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPNOSUPPORT", Py_BuildValue("i", YDB_ERR_TPNOSUPPORT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNUSEDMSG1544", Py_BuildValue("i", YDB_ERR_UNUSEDMSG1544));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZATRANSERR", Py_BuildValue("i", YDB_ERR_ZATRANSERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILTERTIMEDOUT", Py_BuildValue("i", YDB_ERR_FILTERTIMEDOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TLSDLLNOOPEN", Py_BuildValue("i", YDB_ERR_TLSDLLNOOPEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TLSINIT", Py_BuildValue("i", YDB_ERR_TLSINIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TLSCONVSOCK", Py_BuildValue("i", YDB_ERR_TLSCONVSOCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TLSHANDSHAKE", Py_BuildValue("i", YDB_ERR_TLSHANDSHAKE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TLSCONNINFO", Py_BuildValue("i", YDB_ERR_TLSCONNINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TLSIOERROR", Py_BuildValue("i", YDB_ERR_TLSIOERROR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TLSRENEGOTIATE", Py_BuildValue("i", YDB_ERR_TLSRENEGOTIATE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLNOTLS", Py_BuildValue("i", YDB_ERR_REPLNOTLS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COLTRANSSTR2LONG", Py_BuildValue("i", YDB_ERR_COLTRANSSTR2LONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKPASS", Py_BuildValue("i", YDB_ERR_SOCKPASS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKACCEPT", Py_BuildValue("i", YDB_ERR_SOCKACCEPT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOSOCKHANDLE", Py_BuildValue("i", YDB_ERR_NOSOCKHANDLE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGLOADFAIL", Py_BuildValue("i", YDB_ERR_TRIGLOADFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SOCKPASSDATAMIX", Py_BuildValue("i", YDB_ERR_SOCKPASSDATAMIX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOGTCMDB", Py_BuildValue("i", YDB_ERR_NOGTCMDB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOUSERDB", Py_BuildValue("i", YDB_ERR_NOUSERDB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSENOTOPEN", Py_BuildValue("i", YDB_ERR_DSENOTOPEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZSOCKETATTR", Py_BuildValue("i", YDB_ERR_ZSOCKETATTR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZSOCKETNOTSOCK", Py_BuildValue("i", YDB_ERR_ZSOCKETNOTSOCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CHSETALREADY", Py_BuildValue("i", YDB_ERR_CHSETALREADY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSEMAXBLKSAV", Py_BuildValue("i", YDB_ERR_DSEMAXBLKSAV));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_BLKINVALID", Py_BuildValue("i", YDB_ERR_BLKINVALID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CANTBITMAP", Py_BuildValue("i", YDB_ERR_CANTBITMAP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_AIMGBLKFAIL", Py_BuildValue("i", YDB_ERR_AIMGBLKFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_YDBDISTUNVERIF", Py_BuildValue("i", YDB_ERR_YDBDISTUNVERIF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTNOAPPEND", Py_BuildValue("i", YDB_ERR_CRYPTNOAPPEND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTNOSEEK", Py_BuildValue("i", YDB_ERR_CRYPTNOSEEK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTNOTRUNC", Py_BuildValue("i", YDB_ERR_CRYPTNOTRUNC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTNOKEYSPEC", Py_BuildValue("i", YDB_ERR_CRYPTNOKEYSPEC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTNOOVERRIDE", Py_BuildValue("i", YDB_ERR_CRYPTNOOVERRIDE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTKEYTOOBIG", Py_BuildValue("i", YDB_ERR_CRYPTKEYTOOBIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTBADWRTPOS", Py_BuildValue("i", YDB_ERR_CRYPTBADWRTPOS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LABELNOTFND", Py_BuildValue("i", YDB_ERR_LABELNOTFND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RELINKCTLERR", Py_BuildValue("i", YDB_ERR_RELINKCTLERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVLINKTMPDIR", Py_BuildValue("i", YDB_ERR_INVLINKTMPDIR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOEDITOR", Py_BuildValue("i", YDB_ERR_NOEDITOR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UPDPROC", Py_BuildValue("i", YDB_ERR_UPDPROC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_HLPPROC", Py_BuildValue("i", YDB_ERR_HLPPROC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLNOHASHTREC", Py_BuildValue("i", YDB_ERR_REPLNOHASHTREC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REMOTEDBNOTRIG", Py_BuildValue("i", YDB_ERR_REMOTEDBNOTRIG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NEEDTRIGUPGRD", Py_BuildValue("i", YDB_ERR_NEEDTRIGUPGRD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REQRLNKCTLRNDWN", Py_BuildValue("i", YDB_ERR_REQRLNKCTLRNDWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RLNKCTLRNDWNSUC", Py_BuildValue("i", YDB_ERR_RLNKCTLRNDWNSUC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RLNKCTLRNDWNFL", Py_BuildValue("i", YDB_ERR_RLNKCTLRNDWNFL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MPROFRUNDOWN", Py_BuildValue("i", YDB_ERR_MPROFRUNDOWN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZPEEKNOJNLINFO", Py_BuildValue("i", YDB_ERR_ZPEEKNOJNLINFO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TLSPARAM", Py_BuildValue("i", YDB_ERR_TLSPARAM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RLNKRECLATCH", Py_BuildValue("i", YDB_ERR_RLNKRECLATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RLNKSHMLATCH", Py_BuildValue("i", YDB_ERR_RLNKSHMLATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JOBLVN2LONG", Py_BuildValue("i", YDB_ERR_JOBLVN2LONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NLRESTORE", Py_BuildValue("i", YDB_ERR_NLRESTORE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PREALLOCATEFAIL", Py_BuildValue("i", YDB_ERR_PREALLOCATEFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NODFRALLOCSUPP", Py_BuildValue("i", YDB_ERR_NODFRALLOCSUPP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LASTWRITERBYPAS", Py_BuildValue("i", YDB_ERR_LASTWRITERBYPAS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TRIGUPBADLABEL", Py_BuildValue("i", YDB_ERR_TRIGUPBADLABEL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_WEIRDSYSTIME", Py_BuildValue("i", YDB_ERR_WEIRDSYSTIME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REPLSRCEXITERR", Py_BuildValue("i", YDB_ERR_REPLSRCEXITERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVZBREAK", Py_BuildValue("i", YDB_ERR_INVZBREAK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVTMPDIR", Py_BuildValue("i", YDB_ERR_INVTMPDIR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ARCTLMAXHIGH", Py_BuildValue("i", YDB_ERR_ARCTLMAXHIGH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ARCTLMAXLOW", Py_BuildValue("i", YDB_ERR_ARCTLMAXLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NONTPRESTART", Py_BuildValue("i", YDB_ERR_NONTPRESTART));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PBNPARMREQ", Py_BuildValue("i", YDB_ERR_PBNPARMREQ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PBNNOPARM", Py_BuildValue("i", YDB_ERR_PBNNOPARM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PBNUNSUPSTRUCT", Py_BuildValue("i", YDB_ERR_PBNUNSUPSTRUCT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PBNINVALID", Py_BuildValue("i", YDB_ERR_PBNINVALID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PBNNOFIELD", Py_BuildValue("i", YDB_ERR_PBNNOFIELD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLDBSEQNOMATCH", Py_BuildValue("i", YDB_ERR_JNLDBSEQNOMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MULTIPROCLATCH", Py_BuildValue("i", YDB_ERR_MULTIPROCLATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVLOCALE", Py_BuildValue("i", YDB_ERR_INVLOCALE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOMORESEMCNT", Py_BuildValue("i", YDB_ERR_NOMORESEMCNT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETQUALPROB", Py_BuildValue("i", YDB_ERR_SETQUALPROB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTRINTEGRITY", Py_BuildValue("i", YDB_ERR_EXTRINTEGRITY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CRYPTKEYRELEASEFAILED", Py_BuildValue("i", YDB_ERR_CRYPTKEYRELEASEFAILED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUREENCRYPTSTART", Py_BuildValue("i", YDB_ERR_MUREENCRYPTSTART));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUREENCRYPTV4NOALLOW", Py_BuildValue("i", YDB_ERR_MUREENCRYPTV4NOALLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ENCRYPTCONFLT", Py_BuildValue("i", YDB_ERR_ENCRYPTCONFLT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLPOOLRECOVERY", Py_BuildValue("i", YDB_ERR_JNLPOOLRECOVERY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOCKTIMINGINTP", Py_BuildValue("i", YDB_ERR_LOCKTIMINGINTP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PBNUNSUPTYPE", Py_BuildValue("i", YDB_ERR_PBNUNSUPTYPE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFHEADLRU", Py_BuildValue("i", YDB_ERR_DBFHEADLRU));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ASYNCIONOV4", Py_BuildValue("i", YDB_ERR_ASYNCIONOV4));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_AIOCANCELTIMEOUT", Py_BuildValue("i", YDB_ERR_AIOCANCELTIMEOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBGLDMISMATCH", Py_BuildValue("i", YDB_ERR_DBGLDMISMATCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBBLKSIZEALIGN", Py_BuildValue("i", YDB_ERR_DBBLKSIZEALIGN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ASYNCIONOMM", Py_BuildValue("i", YDB_ERR_ASYNCIONOMM));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RESYNCSEQLOW", Py_BuildValue("i", YDB_ERR_RESYNCSEQLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBNULCOL", Py_BuildValue("i", YDB_ERR_DBNULCOL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UTF16ENDIAN", Py_BuildValue("i", YDB_ERR_UTF16ENDIAN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OFRZACTIVE", Py_BuildValue("i", YDB_ERR_OFRZACTIVE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OFRZAUTOREL", Py_BuildValue("i", YDB_ERR_OFRZAUTOREL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OFRZCRITREL", Py_BuildValue("i", YDB_ERR_OFRZCRITREL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OFRZCRITSTUCK", Py_BuildValue("i", YDB_ERR_OFRZCRITSTUCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_OFRZNOTHELD", Py_BuildValue("i", YDB_ERR_OFRZNOTHELD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_AIOBUFSTUCK", Py_BuildValue("i", YDB_ERR_AIOBUFSTUCK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBDUPNULCOL", Py_BuildValue("i", YDB_ERR_DBDUPNULCOL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CHANGELOGINTERVAL", Py_BuildValue("i", YDB_ERR_CHANGELOGINTERVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBNONUMSUBS", Py_BuildValue("i", YDB_ERR_DBNONUMSUBS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_AUTODBCREFAIL", Py_BuildValue("i", YDB_ERR_AUTODBCREFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RNDWNSTATSDBFAIL", Py_BuildValue("i", YDB_ERR_RNDWNSTATSDBFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STATSDBNOTSUPP", Py_BuildValue("i", YDB_ERR_STATSDBNOTSUPP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TPNOSTATSHARE", Py_BuildValue("i", YDB_ERR_TPNOSTATSHARE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FNTRANSERROR", Py_BuildValue("i", YDB_ERR_FNTRANSERROR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOCRENETFILE", Py_BuildValue("i", YDB_ERR_NOCRENETFILE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DSKSPCCHK", Py_BuildValue("i", YDB_ERR_DSKSPCCHK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOCREMMBIJ", Py_BuildValue("i", YDB_ERR_NOCREMMBIJ));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FILECREERR", Py_BuildValue("i", YDB_ERR_FILECREERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RAWDEVUNSUP", Py_BuildValue("i", YDB_ERR_RAWDEVUNSUP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFILECREATED", Py_BuildValue("i", YDB_ERR_DBFILECREATED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PCTYRESERVED", Py_BuildValue("i", YDB_ERR_PCTYRESERVED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_REGFILENOTFOUND", Py_BuildValue("i", YDB_ERR_REGFILENOTFOUND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DRVLONGJMP", Py_BuildValue("i", YDB_ERR_DRVLONGJMP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVSTATSDB", Py_BuildValue("i", YDB_ERR_INVSTATSDB));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STATSDBERR", Py_BuildValue("i", YDB_ERR_STATSDBERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STATSDBINUSE", Py_BuildValue("i", YDB_ERR_STATSDBINUSE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STATSDBFNERR", Py_BuildValue("i", YDB_ERR_STATSDBFNERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSWITCHRETRY", Py_BuildValue("i", YDB_ERR_JNLSWITCHRETRY));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLSWITCHFAIL", Py_BuildValue("i", YDB_ERR_JNLSWITCHFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CLISTRTOOLONG", Py_BuildValue("i", YDB_ERR_CLISTRTOOLONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LVMONBADVAL", Py_BuildValue("i", YDB_ERR_LVMONBADVAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RESTRICTEDOP", Py_BuildValue("i", YDB_ERR_RESTRICTEDOP));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_RESTRICTSYNTAX", Py_BuildValue("i", YDB_ERR_RESTRICTSYNTAX));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MUCREFILERR", Py_BuildValue("i", YDB_ERR_MUCREFILERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLBUFFPHS2SALVAGE", Py_BuildValue("i", YDB_ERR_JNLBUFFPHS2SALVAGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLPOOLPHS2SALVAGE", Py_BuildValue("i", YDB_ERR_JNLPOOLPHS2SALVAGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MURNDWNARGLESS", Py_BuildValue("i", YDB_ERR_MURNDWNARGLESS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFREEZEON", Py_BuildValue("i", YDB_ERR_DBFREEZEON));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DBFREEZEOFF", Py_BuildValue("i", YDB_ERR_DBFREEZEOFF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STPCRIT", Py_BuildValue("i", YDB_ERR_STPCRIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STPOFLOW", Py_BuildValue("i", YDB_ERR_STPOFLOW));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SYSUTILCONF", Py_BuildValue("i", YDB_ERR_SYSUTILCONF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MSTACKSZNA", Py_BuildValue("i", YDB_ERR_MSTACKSZNA));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_JNLEXTRCTSEQNO", Py_BuildValue("i", YDB_ERR_JNLEXTRCTSEQNO));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVSEQNOQUAL", Py_BuildValue("i", YDB_ERR_INVSEQNOQUAL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOWSPC", Py_BuildValue("i", YDB_ERR_LOWSPC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FAILEDRECCOUNT", Py_BuildValue("i", YDB_ERR_FAILEDRECCOUNT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOADRECCNT", Py_BuildValue("i", YDB_ERR_LOADRECCNT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_COMMFILTERERR", Py_BuildValue("i", YDB_ERR_COMMFILTERERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOFILTERNEST", Py_BuildValue("i", YDB_ERR_NOFILTERNEST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MLKHASHTABERR", Py_BuildValue("i", YDB_ERR_MLKHASHTABERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LOCKCRITOWNER", Py_BuildValue("i", YDB_ERR_LOCKCRITOWNER));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MLKHASHWRONG", Py_BuildValue("i", YDB_ERR_MLKHASHWRONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_XCRETNULLREF", Py_BuildValue("i", YDB_ERR_XCRETNULLREF));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXTCALLBOUNDS", Py_BuildValue("i", YDB_ERR_EXTCALLBOUNDS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_EXCEEDSPREALLOC", Py_BuildValue("i", YDB_ERR_EXCEEDSPREALLOC));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ZTIMEOUT", Py_BuildValue("i", YDB_ERR_ZTIMEOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ERRWZTIMEOUT", Py_BuildValue("i", YDB_ERR_ERRWZTIMEOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MLKHASHRESIZE", Py_BuildValue("i", YDB_ERR_MLKHASHRESIZE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MLKHASHRESIZEFAIL", Py_BuildValue("i", YDB_ERR_MLKHASHRESIZEFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MLKCLEANED", Py_BuildValue("i", YDB_ERR_MLKCLEANED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NOTMNAME", Py_BuildValue("i", YDB_ERR_NOTMNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_DEVNAMERESERVED", Py_BuildValue("i", YDB_ERR_DEVNAMERESERVED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKREL", Py_BuildValue("i", YDB_ERR_ORLBKREL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_ORLBKRESTART", Py_BuildValue("i", YDB_ERR_ORLBKRESTART));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNIQNAME", Py_BuildValue("i", YDB_ERR_UNIQNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_APDINITFAIL", Py_BuildValue("i", YDB_ERR_APDINITFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_APDCONNFAIL", Py_BuildValue("i", YDB_ERR_APDCONNFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_APDLOGFAIL", Py_BuildValue("i", YDB_ERR_APDLOGFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STATSDBMEMERR", Py_BuildValue("i", YDB_ERR_STATSDBMEMERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_QUERY2", Py_BuildValue("i", YDB_ERR_QUERY2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MIXIMAGE", Py_BuildValue("i", YDB_ERR_MIXIMAGE));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_LIBYOTTAMISMTCH", Py_BuildValue("i", YDB_ERR_LIBYOTTAMISMTCH));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_READONLYNOSTATS", Py_BuildValue("i", YDB_ERR_READONLYNOSTATS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_READONLYLKFAIL", Py_BuildValue("i", YDB_ERR_READONLYLKFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVVARNAME", Py_BuildValue("i", YDB_ERR_INVVARNAME));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_PARAMINVALID", Py_BuildValue("i", YDB_ERR_PARAMINVALID));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INSUFFSUBS", Py_BuildValue("i", YDB_ERR_INSUFFSUBS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_MINNRSUBSCRIPTS", Py_BuildValue("i", YDB_ERR_MINNRSUBSCRIPTS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SUBSARRAYNULL", Py_BuildValue("i", YDB_ERR_SUBSARRAYNULL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FATALERROR1", Py_BuildValue("i", YDB_ERR_FATALERROR1));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NAMECOUNT2HI", Py_BuildValue("i", YDB_ERR_NAMECOUNT2HI));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVNAMECOUNT", Py_BuildValue("i", YDB_ERR_INVNAMECOUNT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_FATALERROR2", Py_BuildValue("i", YDB_ERR_FATALERROR2));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TIME2LONG", Py_BuildValue("i", YDB_ERR_TIME2LONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_VARNAME2LONG", Py_BuildValue("i", YDB_ERR_VARNAME2LONG));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIMPLEAPINEST", Py_BuildValue("i", YDB_ERR_SIMPLEAPINEST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CALLINTCOMMIT", Py_BuildValue("i", YDB_ERR_CALLINTCOMMIT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_CALLINTROLLBACK", Py_BuildValue("i", YDB_ERR_CALLINTROLLBACK));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_TCPCONNTIMEOUT", Py_BuildValue("i", YDB_ERR_TCPCONNTIMEOUT));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STDERRALREADYOPEN", Py_BuildValue("i", YDB_ERR_STDERRALREADYOPEN));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SETENVFAIL", Py_BuildValue("i", YDB_ERR_SETENVFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNSETENVFAIL", Py_BuildValue("i", YDB_ERR_UNSETENVFAIL));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_UNKNOWNSYSERR", Py_BuildValue("i", YDB_ERR_UNKNOWNSYSERR));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STRUCTNOTALLOCD", Py_BuildValue("i", YDB_ERR_STRUCTNOTALLOCD));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_NODEEND", Py_BuildValue("i", YDB_ERR_NODEEND));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVLNPAIRLIST", Py_BuildValue("i", YDB_ERR_INVLNPAIRLIST));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_INVTPTRANS", Py_BuildValue("i", YDB_ERR_INVTPTRANS));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_THREADEDAPINOTALLOWED", Py_BuildValue("i", YDB_ERR_THREADEDAPINOTALLOWED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_SIMPLEAPINOTALLOWED", Py_BuildValue("i", YDB_ERR_SIMPLEAPINOTALLOWED));
	PyDict_SetItemString(module_dictionary, "YDB_ERR_STAPIFORKEXEC", Py_BuildValue("i", YDB_ERR_STAPIFORKEXEC));
	//PyDict_SetItemString(module_dictionary, "YDB_ERR_INVSIGNM", Py_BuildValue("i", YDB_ERR_INVSIGNM)); /* not available in 1.26 */

	/* Excetpions */
	/* setting up YottaDBError */
	PyObject* exc_dict = make_getter_code();
	if (exc_dict == NULL)
		return NULL;
	
	YottaDBError = PyErr_NewException("_yottadb.YottaDBError",
										NULL, // use to pick base class
										exc_dict);
	PyModule_AddObject(module,"YottaDBError", YottaDBError);
	YottaDBLockTimeout = PyErr_NewException("_yottadb.YottaDBLockTimeout",
										NULL, // use to pick base class
										NULL);
	/* setting up YottaDBLockTimeout */
	PyModule_AddObject(module,"YottaDBLockTimeout", YottaDBLockTimeout);

	return module;
}