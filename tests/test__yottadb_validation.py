import pytest # type: ignore
import _yottadb

BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH = b'1'*(2**31 + 1)

def varname_invalid(function):
    with pytest.raises(TypeError):
        function(varname='b')

    try:
        function(varname=b'b' * (_yottadb.YDB_MAX_IDENT)) # almost too long
    except _yottadb.YDBError: # testing c-extentions validation not YottaDB's
        pass
    with pytest.raises(ValueError):
            function(varname=b'b'*(_yottadb.YDB_MAX_IDENT + 1))

    with pytest.raises(ValueError):
            function(varname=BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH)


def subsarray_invalid(function):
    with pytest.raises(TypeError):
        function(b'test', b'this is the wrong kind of sequence')

    try:
        function(b'test', (b'b',) * (_yottadb.YDB_MAX_SUBS)) # almost too many
    except _yottadb.YDBError: # testing c-extentions validation not YottaDB's
        pass

    with pytest.raises(ValueError):
            function(b'test', (b'b',)*(_yottadb.YDB_MAX_SUBS + 1))

    with pytest.raises(TypeError):
            function(b'test', ('not a bytes object',))

    try:
        function(b'test', (b'b' * (_yottadb.YDB_MAX_STR),)) # almost too long
    except _yottadb.YDBError: # testing c-extentions validation not YottaDB's
        pass

    with pytest.raises(ValueError):
            function(b'test', (b'b'*(_yottadb.YDB_MAX_STR + 1),))

    with pytest.raises(ValueError):
            function(b'test', (BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH,))

# data()
def test_data_varname(ydb):
    varname_invalid(_yottadb.data)

def test_data_subsarray(ydb):
    subsarray_invalid(_yottadb.data)

# delete()
def test_delete_varname(ydb):
    varname_invalid(_yottadb.delete)

def test_delete_subsarray(ydb):
    subsarray_invalid(_yottadb.delete)

# delete_excel()
def test_delete_excel_varnames(ydb):
    with pytest.raises(TypeError):
        _yottadb.delete_excel(varnames='not a sequence')

    with pytest.raises(TypeError):
        _yottadb.delete_excel(varnames=('not a sequence of bytes',))

    _yottadb.delete_excel(varnames=[b'test' + bytes(str(x), encoding='utf-8') for x in range(0, _yottadb.YDB_MAX_NAMES)])
    with pytest.raises(ValueError):
        _yottadb.delete_excel(varnames=[b'test'+bytes(str(x), encoding='utf-8') for x in range(0, _yottadb.YDB_MAX_NAMES+1)])

    with pytest.raises(ValueError):
        _yottadb.delete_excel(varnames=[b'b'*(_yottadb.YDB_MAX_IDENT + 1)])


# get()
def test_get_varname(ydb):
    varname_invalid(_yottadb.get)

def test_get_subsarray(ydb):
    subsarray_invalid(_yottadb.get)

# incr()
def test_incr_varname(ydb):
    varname_invalid(_yottadb.incr)

def test_incr_subsarray(ydb):
    subsarray_invalid(_yottadb.incr)

def test_incr_increment(ydb):
    key = {'varname':b'test', 'subsarray':(b'b',)}
    with pytest.raises(TypeError):
        _yottadb.incr(**key, increment='not bytes')

    try:
        _yottadb.incr(**key, increment=b'1' * (_yottadb.YDB_MAX_STR))
    except _yottadb.YDBError: # testing c-extentions validation not YottaDB's
        pass

    with pytest.raises(ValueError):
            _yottadb.incr(**key, increment=b'1'*(_yottadb.YDB_MAX_STR + 1))

    with pytest.raises(ValueError):
            _yottadb.incr(**key, increment=BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH)

# lock()
def test_lock_keys(ydb):
    # keys argument type
    with pytest.raises(TypeError):
        _yottadb.lock('not list or tuple')

    # keys argument length
    keys = [[b'test' + bytes(str(x), encoding='utf-8')] for x in range(0, _yottadb.YDB_MAX_NAMES)]
    _yottadb.lock(keys)
    with pytest.raises(ValueError):
        keys = [[b'test' + bytes(str(x), encoding='utf-8')] for x in range(0, _yottadb.YDB_MAX_NAMES + 1)]
        _yottadb.lock(keys)

    # keys argument item (key) type
    with pytest.raises(TypeError):
        _yottadb.lock(('not list or tuple',))

    # keys argument item (key) length
    with pytest.raises(ValueError):
        _yottadb.lock(([],)) # not enough
    with pytest.raises(ValueError):
        _yottadb.lock(([b'varname', [b'subscript'], b'extra'],)) #too many

    # keys argument item (key) first element(varname) type
    with pytest.raises(TypeError):
        _yottadb.lock((('test',),))

    # keys argument item (key) first element(varname) length
    _yottadb.lock(((b'a' * (_yottadb.YDB_MAX_IDENT),),))
    with pytest.raises(ValueError):
        _yottadb.lock(((b'a' * (_yottadb.YDB_MAX_IDENT + 1),),))

    # keys argument item (key) second element (subsarray) type
    with pytest.raises(TypeError):
        _yottadb.lock(((b'test', 'not list or tuple'),))

    # keys argument item (key) second element (subsarray) length
    subsarray = [b'test' + bytes(str(x), encoding='utf-8') for x in range(0, _yottadb.YDB_MAX_SUBS)]
    _yottadb.lock(((b'test', subsarray,),))
    with pytest.raises(ValueError):
        subsarray = [b'test' + bytes(str(x), encoding='utf-8') for x in range(0, _yottadb.YDB_MAX_SUBS + 1)]
        _yottadb.lock(((b'test', subsarray,),))

    # keys argument item (key) second element (subsarray) item (subscript) type
    with pytest.raises(TypeError):
        _yottadb.lock(((b'test', ['not bytes']),))

    # keys argument item (key) second element (subsarray) item (subscript) length
    try:
        _yottadb.lock(((b'test', [b'a' * (_yottadb.YDB_MAX_STR)]),))
    except _yottadb.YDBError: # testing c-extentions validation not YottaDB's
        pass
    with pytest.raises(ValueError):
        _yottadb.lock(((b'test', [b'a' * (_yottadb.YDB_MAX_STR + 1)]),))


# lock_decr()
def test_decr_varname(ydb):
    varname_invalid(_yottadb.lock_decr)

def test_decr_subsarray(ydb):
    subsarray_invalid(_yottadb.lock_decr)

# lock_incr()
def test_lock_incr_varname(ydb):
    varname_invalid(_yottadb.lock_incr)

def test_lock_incr_subsarray(ydb):
    subsarray_invalid(_yottadb.lock_incr)

# node_next()
def test_node_next_varname(ydb):
    varname_invalid(_yottadb.node_next)

def test_node_next_subsarray(ydb):
    subsarray_invalid(_yottadb.node_next)

# node_previous()
def test_node_previous_varname(ydb):
    varname_invalid(_yottadb.node_previous)

def test_node_previous_subsarray(ydb):
    subsarray_invalid(_yottadb.node_previous)

# set()
def test_set_varname(ydb):
    varname_invalid(_yottadb.set)

def test_set_subsarray(ydb):
    subsarray_invalid(_yottadb.set)

def test_set_value(ydb):
    key = {'varname':b'test', 'subsarray':(b'b',)}
    with pytest.raises(TypeError):
        _yottadb.set(**key, value='not bytes')

    _yottadb.set(**key, value=b'b' * (_yottadb.YDB_MAX_STR))
    with pytest.raises(ValueError):
            _yottadb.set(**key, value=b'b'*(_yottadb.YDB_MAX_STR + 1))

    with pytest.raises(ValueError):
            _yottadb.set(**key, value=BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH)

# str2zwr()
# subscript_next()
def test_subscript_next_varname(ydb):
    varname_invalid(_yottadb.subscript_next)

def test_subscript_next_subsarray(ydb):
    subsarray_invalid(_yottadb.subscript_next)

# subscript_previous()
def test_subscript_previous_varname(ydb):
    varname_invalid(_yottadb.subscript_previous)

def test_subscript_previous_subsarray(ydb):
    subsarray_invalid(_yottadb.subscript_previous)
# tp()
def simple_transaction(tp_token:int) -> None:
    return _yottadb.YDB_OK

def test_tp_callback(ydb):
    with pytest.raises(TypeError):
        _yottadb.tp(callback="not a callable")

def test_tp_args(ydb):
    with pytest.raises(TypeError):
        _yottadb.tp(callback=simple_transaction, args="not a sequence of arguments")

def test_tp_kwargs(ydb):
    with pytest.raises(TypeError):
        _yottadb.tp(callback=simple_transaction, kwargs="not a dictionary of keyword arguments")

def test_delete_excel_varnames(ydb):
    with pytest.raises(TypeError):
        _yottadb.tp(callback=simple_transaction, varnames='not a sequence')

    with pytest.raises(TypeError):
        _yottadb.tp(callback=simple_transaction, varnames=('not a sequence of bytes',))

    _yottadb.tp(callback=simple_transaction,
                varnames=[b'test' + bytes(str(x), encoding='utf-8') for x in range(0, _yottadb.YDB_MAX_NAMES)])
    with pytest.raises(ValueError):
        _yottadb.tp(callback=simple_transaction,
                    varnames=[b'test'+bytes(str(x), encoding='utf-8') for x in range(0, _yottadb.YDB_MAX_NAMES+1)])

    _yottadb.tp(callback=simple_transaction, varnames=[b'b' * (_yottadb.YDB_MAX_IDENT)])
    with pytest.raises(ValueError):
        _yottadb.tp(callback=simple_transaction, varnames=[b'b'*(_yottadb.YDB_MAX_IDENT + 1)])

# zwr2str()