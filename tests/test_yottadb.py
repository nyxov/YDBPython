#################################################################
#                                                               #
# Copyright (c) 2019 Peter Goss All rights reserved.            #
#                                                               #
# Copyright (c) 2019 YottaDB LLC and/or its subsidiaries.       #
# All rights reserved.                                          #
#                                                               #
#	This source code contains the intellectual property         #
#	of its copyright holder(s), and is made available           #
#	under a license.  If you do not know the terms of           #
#	the license, please stop and do not read further.           #
#                                                               #
#################################################################
import pytest # type: ignore


import yottadb
from yottadb import api as api



def test_set_get_in_encoding_str(ydb:yottadb.Context, text_from_files):
    encoding = text_from_files['encoding']
    text_str = text_from_files['text_str']

    ydb.set(varname='testencoding', value=text_str, val_encoding=encoding)
    assert ydb.get(varname='testencoding', val_encoding=encoding) == text_str

def test_set_get_in_encoding_bytes(ydb:yottadb.Context, text_from_files):
    encoding = text_from_files['encoding']
    text_str = text_from_files['text_str']
    text_bytes = text_from_files['text_bytes']

    ydb.set(varname='testencoding', value=text_bytes, val_encoding=None)
    assert ydb.get(varname='testencoding', val_encoding=None) == text_bytes
    assert ydb.get(varname='testencoding', val_encoding=encoding) == text_str

def test_set_get_in_encoding_default(ydb:yottadb.Context, text_from_files):
    encoding = text_from_files['encoding']
    text_str = text_from_files['text_str']
    text_bytes = text_from_files['text_bytes']
    context = yottadb.Context(api=ydb.api, val_encoding=encoding)

    context.set('testencoding', value=text_str)
    assert context.get('testencoding') == text_str
    assert context.get('testencoding', val_encoding=None) == text_bytes

def test_set_get_with_encoded_subscripts_defaults(ydb, text_from_files):
    encoding = text_from_files['encoding']
    text_str = text_from_files['text_str']
    context = yottadb.Context(api=ydb.api, subs_encoding=encoding)

    context.set('testencoding', (text_str, text_str), 'test')
    assert context.get('testencoding', (text_str, text_str)) == 'test'


def test_set_get_with_encoded_subscripts_str(ydb, text_from_files):
    encoding = text_from_files['encoding']
    text_str = text_from_files['text_str']

    ydb.set('testencoding', (text_str, text_str), 'test', subs_encoding=encoding)
    assert ydb.get('testencoding', (text_str, text_str), subs_encoding=encoding) == 'test'
    with pytest.raises(yottadb.YottaDBError) as e:
        ydb.get('testencoding', (text_str, text_str))
        assert e.code == yottadb.LOCAL_VAR_UNDEF

def test_set_get_with_encoded_subscripts_bytes(ydb, text_from_files):
    encoding = text_from_files['encoding']
    text_bytes = text_from_files['text_bytes']
    text_str = text_from_files['text_str']

    ydb.set('testencoding', (text_bytes, text_bytes), 'test', subs_encoding=None)
    assert ydb.get('testencoding', (text_bytes, text_bytes), subs_encoding=None) == 'test'
    with pytest.raises(yottadb.YottaDBError) as e:
        assert ydb.get('testencoding', (text_str, text_str)) == 'test'
        assert e.code == yottadb.LOCAL_VAR_UNDEF


def test_key_smoke_test1(ydb, simple_data):
    key = ydb['^test1']
    assert key.value == 'test1value'
    assert key.name == '^test1'
    assert key.name_bytes == b'^test1'
    assert key.varname_key == key
    assert key.varname == '^test1'
    assert key.varname_bytes == b'^test1'
    assert key.subsarray == []
    assert key.subsarray_bytes == []

def test_key_smoke_test2(ydb, simple_data):
    key = ydb['^test2']['sub1']
    assert key.value == 'test2value'
    assert key.name == 'sub1'
    assert key.name_bytes == b'sub1'
    assert key.varname_key == ydb['^test2']
    assert key.varname == '^test2'
    assert key.varname_bytes == b'^test2'
    assert key.subsarray == ['sub1']
    assert key.subsarray_bytes == [b'sub1']


def test_key_smoke_test3(ydb):
    key = ydb['test3local']['sub1']
    key.value = 'smoketest3local'
    assert key.value == 'smoketest3local'
    assert key.name == 'sub1'
    assert key.name_bytes == b'sub1'
    assert key.varname_key == ydb['test3local']
    assert key.varname == 'test3local'
    assert key.varname_bytes == b'test3local'
    assert key.subsarray == ['sub1']
    assert key.subsarray_bytes == [b'sub1']



def test_Key_construction_error(ydb):
    with pytest.raises(TypeError):
        yottadb.Key('^test1', 'not a Key object')



def test_Key_get_value1(ydb, simple_data):
    assert ydb["^test1"].value == "test1value"

def test_Key_get_value2(ydb, simple_data):
    assert ydb["^test2"]['sub1'].value == "test2value"

def test_Key_get_value3(ydb, simple_data):
    assert ydb["^test3"].value =="test3value1"
    assert ydb["^test3"]['sub1'].value == "test3value2"
    assert ydb["^test3"]['sub1']['sub2'].value == "test3value3"

def test_Key_subsarray(ydb, simple_data):
    assert ydb["^test3"].subsarray == []
    assert ydb["^test3"]['sub1'].subsarray == ['sub1']
    assert ydb["^test3"]['sub1']['sub2'].subsarray == ['sub1', 'sub2']

def test_Key_varname(ydb, simple_data):
    assert ydb["^test3"].varname == "^test3"
    assert ydb["^test3"]['sub1'].varname == "^test3"
    assert ydb["^test3"]['sub1']['sub2'].varname == "^test3"

def test_Key_set_value1(ydb):
    testkey = ydb['test4']
    testkey.value = 'test4value'
    assert testkey.value == 'test4value'

def test_Key_set_value2(ydb):
    testkey = ydb['test5']['sub1']
    testkey.value = 'test5value'
    assert testkey.value == 'test5value'
    assert ydb['test5']['sub1'].value == 'test5value'

def test_Key_delete_node(ydb):
    testkey = ydb['test6']
    subkey = testkey['sub1']
    testkey.value = 'test6value'
    subkey.value = 'test6 subvalue'

    assert testkey.value =='test6value'
    assert subkey.value =='test6 subvalue'

    testkey.delete_node()

    assert testkey.value ==None
    assert subkey.value =='test6 subvalue'

def test_Key_delete_tree(ydb):
    testkey = ydb['test7']
    subkey = testkey['sub1']
    testkey.value = 'test7value'
    subkey.value = 'test7 subvalue'

    assert testkey.value =='test7value'
    assert subkey.value =='test7 subvalue'

    testkey.delete_tree()

    assert testkey.value ==None
    assert subkey.value ==None


def test_Key_data(ydb, simple_data):
    assert ydb['nodata'].data == yottadb.DATA_NO_DATA
    assert ydb['^test1'].data == yottadb.DATA_HAS_VALUE_NO_TREE
    assert ydb['^test2'].data == yottadb.DATA_NO_VALUE_HAS_TREE
    assert ydb['^test2']['sub1'].data == yottadb.DATA_HAS_VALUE_NO_TREE
    assert ydb['^test3'].data == yottadb.DATA_HAS_VALUE_HAS_TREE
    assert ydb['^test3']['sub1'].data == yottadb.DATA_HAS_VALUE_HAS_TREE
    assert ydb['^test3']['sub1']['sub2'].data == yottadb.DATA_HAS_VALUE_NO_TREE


def test_Key_has_value(ydb, simple_data):
    assert ydb['nodata'].has_value == False
    assert ydb['^test1'].has_value == True
    assert ydb['^test2'].has_value == False
    assert ydb['^test2']['sub1'].has_value == True
    assert ydb['^test3'].has_value == True
    assert ydb['^test3']['sub1'].has_value == True
    assert ydb['^test3']['sub1']['sub2'].has_value == True

def test_Key_has_tree(ydb, simple_data):
    assert ydb['nodata'].has_tree == False
    assert ydb['^test1'].has_tree == False
    assert ydb['^test2'].has_tree == True
    assert ydb['^test2']['sub1'].has_tree == False
    assert ydb['^test3'].has_tree == True
    assert ydb['^test3']['sub1'].has_tree == True
    assert ydb['^test3']['sub1']['sub2'].has_tree == False