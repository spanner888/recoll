/* Copyright (C) 2007 J.F.Dockes
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


#include <Python.h>
#include <structmember.h>

#include <strings.h>

#include <string>
#include <iostream>
#include <set>
using namespace std;

#include "rclinit.h"
#include "rclconfig.h"
#include "rcldb.h"
#include "searchdata.h"
#include "rclquery.h"
#include "pathut.h"
#include "wasastringtoquery.h"
#include "wasatorcl.h"
#include "debuglog.h"
#include "pathut.h"

static set<Rcl::Db *> the_dbs;
static set<Rcl::Query *> the_queries;
static set<Rcl::Doc *> the_docs;

static RclConfig *rclconfig;

// This has to exist somewhere in the python api ??
PyObject *obj_Create(PyTypeObject *tp, PyObject *args, PyObject *kwargs)
{
    PyObject *result = tp->tp_new(tp, args, kwargs);
    if (result && tp->tp_init(result, args, kwargs) < 0)
	return 0;
    return result;
}

//////////////////////////////////////////////////////////////////////
/// SEARCHDATA SearchData code
typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    RefCntr<Rcl::SearchData> sd;
} recoll_SearchDataObject;

static void 
SearchData_dealloc(recoll_SearchDataObject *self)
{
    LOGDEB(("SearchData_dealloc\n"));
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
SearchData_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    LOGDEB(("SearchData_new\n"));
    recoll_SearchDataObject *self;

    self = (recoll_SearchDataObject *)type->tp_alloc(type, 0);
    if (self == 0) 
	return 0;
    return (PyObject *)self;
}

static int
SearchData_init(recoll_SearchDataObject *self, PyObject *args, PyObject *kwargs)
{
    LOGDEB(("SearchData_init\n"));
    static const char* kwlist[] = {"type", NULL};
    char *stp = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|s", (char**)kwlist, &stp))
	return -1;
    Rcl::SClType tp = Rcl::SCLT_AND;

    if (stp && strcasecmp(stp, "or")) {
	tp = Rcl::SCLT_OR;
    }
    self->sd = RefCntr<Rcl::SearchData>(new Rcl::SearchData(tp, "english"));
    return 0;
}

/* Note: addclause necessite And/Or vient du fait que le string peut avoir
   plusieurs mots. A transferer dans l'i/f Python ou pas ? */
PyDoc_STRVAR(doc_addclause,
"addclause(type='and'|'or'|'excl'|'phrase'|'near'|'sub',\n"
"          qstring=string, slack=int, field=string, stemming=1|0,\n"
"          subSearch=SearchData)\n"
"Adds a simple clause to the SearchData And/Or chain, or a subquery\n"
"defined by another SearchData object\n"
);

/* Forward declaration only, definition needs recoll_searchDataType */
static PyObject *
SearchData_addclause(recoll_SearchDataObject* self, PyObject *args, 
		     PyObject *kwargs);



static PyMethodDef SearchData_methods[] = {
    {"addclause", (PyCFunction)SearchData_addclause, METH_VARARGS|METH_KEYWORDS,
     doc_addclause},
    {NULL}  /* Sentinel */
};

PyDoc_STRVAR(doc_SearchDataObject,
"SearchData()\n"
"\n"
"A SearchData object describes a query. It has a number of global\n"
"parameters and a chain of search clauses.\n"
);
static PyTypeObject recoll_SearchDataType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "recoll.SearchData",             /*tp_name*/
    sizeof(recoll_SearchDataObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)SearchData_dealloc,    /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,  /*tp_flags*/
    doc_SearchDataObject,      /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    SearchData_methods,        /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)SearchData_init, /* tp_init */
    0,                         /* tp_alloc */
    SearchData_new,            /* tp_new */
};

static PyObject *
SearchData_addclause(recoll_SearchDataObject* self, PyObject *args, 
		     PyObject *kwargs)
{
    LOGDEB(("SearchData_addclause\n"));
    if (self->sd.isNull()) {
	LOGERR(("SearchData_addclause: not init??\n"));
        PyErr_SetString(PyExc_AttributeError, "sd");
        return 0;
    }
    static const char *kwlist[] = {"type", "qstring", "slack", "field", 
                                   "stemming", "subsearch", NULL};
    char *tp = 0;
    char *qs = 0; // needs freeing
    int slack = 0;
    char *fld = 0; // needs freeing
    int  dostem = 1; // needs freeing
    recoll_SearchDataObject *sub = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ses|iesiO!", (char**)kwlist,
				     &tp, "utf-8", &qs, &slack,
				     "utf-8", &fld, &dostem,
				     &recoll_SearchDataType, &sub))
	return 0;

    Rcl::SearchDataClause *cl = 0;

    switch (tp[0]) {
    case 'a':
    case 'A':
	if (strcasecmp(tp, "and"))
	    goto defaultcase;
	cl = new Rcl::SearchDataClauseSimple(Rcl::SCLT_AND, qs, fld?fld:"");
	break;
    case 'o':
    case 'O':
	if (strcasecmp(tp, "or"))
	    goto defaultcase;
	cl = new Rcl::SearchDataClauseSimple(Rcl::SCLT_OR, qs, fld?fld:"");
	break;
    case 'e':
    case 'E':
	if (strcasecmp(tp, "excl"))
	    goto defaultcase;
	cl = new Rcl::SearchDataClauseSimple(Rcl::SCLT_EXCL, qs, fld?fld:"");
	break;
    case 'n':
    case 'N':
	if (strcasecmp(tp, "near"))
	    goto defaultcase;
	cl = new Rcl::SearchDataClauseDist(Rcl::SCLT_NEAR, qs, slack, 
					   fld ? fld : "");
	break;
    case 'p':
    case 'P':
	if (strcasecmp(tp, "phrase"))
	    goto defaultcase;
	cl = new Rcl::SearchDataClauseDist(Rcl::SCLT_PHRASE, qs, slack, 
					   fld ? fld : "");
	break;
    case 's':
    case 'S':
	if (strcasecmp(tp, "sub"))
	    goto defaultcase;
	cl = new Rcl::SearchDataClauseSub(Rcl::SCLT_SUB, sub->sd);
	break;
    defaultcase:
    default:
        PyErr_SetString(PyExc_AttributeError, "Bad tp arg");
	return 0;
    }
    if (dostem == 0) {
	cl->setModifiers(Rcl::SearchDataClause::SDCM_NOSTEMMING);
    }

    PyMem_Free(qs);
    PyMem_Free(fld);
    self->sd->addClause(cl);
    Py_RETURN_NONE;
}


///////////////////////////////////////////////////////////////////////
///// DOC Doc code
typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    Rcl::Doc *doc;
} recoll_DocObject;

static void 
Doc_dealloc(recoll_DocObject *self)
{
    LOGDEB(("Doc_dealloc\n"));
    if (self->doc)
	the_docs.erase(self->doc);
    delete self->doc;
    self->doc = 0;
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
Doc_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    recoll_DocObject *self;
    LOGDEB(("Doc_new\n"));

    self = (recoll_DocObject *)type->tp_alloc(type, 0);
    if (self == 0) 
	return 0;
    self->doc = 0;
    return (PyObject *)self;
}

static int
Doc_init(recoll_DocObject *self, PyObject *, PyObject *)
{
    LOGDEB(("Doc_init\n"));
    if (self->doc)
	the_docs.erase(self->doc);
    delete self->doc;
    self->doc = new Rcl::Doc;
    if (self->doc == 0)
	return -1;
    the_docs.insert(self->doc);
    return 0;
}

PyDoc_STRVAR(doc_getbinurl,
"getbinurl(none) -> binary url\n"
"\n"
"Returns an URL with a path part which is a as bit for bit copy of the \n"
"file system path, without encoding\n"
);

static PyObject *
Doc_getbinurl(recoll_DocObject *self)
{
    LOGDEB(("Doc_getbinurl\n"));
    if (self->doc == 0 || 
	the_docs.find(self->doc) == the_docs.end()) {
        PyErr_SetString(PyExc_AttributeError, "doc");
	return 0;
    }
    return PyByteArray_FromStringAndSize(self->doc->url.c_str(), 
					 self->doc->url.size());
}

PyDoc_STRVAR(doc_setbinurl,
"setbinurl(url) -> binary url\n"
"\n"
"Set the URL from binary path like file://may/contain/unencodable/bytes\n"
);

static int
Doc_setbinurl(recoll_DocObject *self, PyObject *value)
{
    if (self->doc == 0 || 
	the_docs.find(self->doc) == the_docs.end()) {
        PyErr_SetString(PyExc_AttributeError, "doc??");
	return -1;
    }

    self->doc->url = string(PyByteArray_AsString(value),
			    PyByteArray_Size(value));
    return 0;
}


static PyMethodDef Doc_methods[] = {
    {"getbinurl", (PyCFunction)Doc_getbinurl, METH_NOARGS,
     doc_getbinurl},
    {"setbinurl", (PyCFunction)Doc_setbinurl, METH_O,
     doc_setbinurl},
    {NULL}  /* Sentinel */
};

static PyObject *
Doc_getattr(recoll_DocObject *self, char *name)
{
    LOGDEB(("Doc_getattr: name [%s]\n", name));
    if (self->doc == 0 || 
	the_docs.find(self->doc) == the_docs.end()) {
        PyErr_SetString(PyExc_AttributeError, "doc");
	return 0;
    }
#if 0
    for (map<string,string>::const_iterator it = self->doc->meta.begin();
	 it != self->doc->meta.end(); it++) {
	LOGDEB(("meta[%s] -> [%s]\n", it->first.c_str(), it->second.c_str()));
    }
#endif
    string key = rclconfig->fieldCanon(string(name));

    // Handle special cases, then check this is not a method then
    // try retrieving key value from the meta array

    string value;
    bool found = false;
    switch (key.at(0)) {
    case 'u':
	if (!key.compare(Rcl::Doc::keyurl)) {
	    value = self->doc->url; found = true;
	}
	break;
    case 'f':
	if (!key.compare(Rcl::Doc::keyfs)) {
	    value = self->doc->fbytes; found = true;
	} else if (!key.compare(Rcl::Doc::keyfs)) {
	    value = self->doc->fbytes; found = true;
	} else if (!key.compare(Rcl::Doc::keyfmt)) {
	    value = self->doc->fmtime; found = true;
	}
	break;
    case 'd':
	if (!key.compare(Rcl::Doc::keyds)) {
	    value = self->doc->dbytes; found = true;
	} else if (!key.compare(Rcl::Doc::keydmt)) {
	    value = self->doc->dmtime; found = true;
	}
	break;
    case 'i':
	if (!key.compare(Rcl::Doc::keyipt)) {
	    value = self->doc->ipath; found = true;
	}
	break;
    case 'm':
	if (!key.compare(Rcl::Doc::keytp)) {
	    value = self->doc->mimetype; found = true;
	} else if (!key.compare(Rcl::Doc::keymt)) {
	    value = self->doc->dmtime.empty() ? self->doc->fmtime : 
		self->doc->dmtime; found = true;
	}
	break;
    case 'o':
	if (!key.compare(Rcl::Doc::keyoc)) {
	    value = self->doc->origcharset; found = true;
	}
	break;
    case 's':
	if (!key.compare(Rcl::Doc::keysig)) {
	    value = self->doc->sig; found = true;
	} else 	if (!key.compare(Rcl::Doc::keysz)) {
	    value = self->doc->dbytes.empty() ? self->doc->fbytes : 
		self->doc->dbytes; found = true;
	}
	break;
    }

    if (!found) {
	PyObject *meth = Py_FindMethod(Doc_methods, (PyObject*)self, 
				       key.c_str());
	if (meth) {
	    return meth;
	} else {
	    PyErr_Clear();
	}

	if (self->doc->getmeta(key, 0)) {
	    value = self->doc->meta[key];
	}
    }

    LOGDEB(("Doc_getattr: [%s] (%s) -> [%s]\n",
	    name, key.c_str(), value.c_str()));
    // Return a python unicode object
    PyObject* res = PyUnicode_Decode(value.c_str(), value.size(), "utf-8",
				     "replace");
    return res;
}

static int
Doc_setattr(recoll_DocObject *self, char *name, PyObject *value)
{
    if (self->doc == 0 || 
	the_docs.find(self->doc) == the_docs.end()) {
        PyErr_SetString(PyExc_AttributeError, "doc??");
	return -1;
    }
    LOGDEB1(("Doc_setmeta: doc %p\n", self->doc));
    if (PyString_Check(value)) {
	value = PyUnicode_FromObject(value);
	if (value == 0) 
	    return -1;
    } 

    if (!PyUnicode_Check(value)) {
	PyErr_SetString(PyExc_AttributeError, "value not str/unicode??");
	return -1;
    }
    if (name == 0) {
        PyErr_SetString(PyExc_AttributeError, "name??");
	return -1;
    }

    PyObject* putf8 = PyUnicode_AsUTF8String(value);
    if (putf8 == 0) {
	LOGERR(("Doc_setmeta: encoding to utf8 failed\n"));
	PyErr_SetString(PyExc_AttributeError, "value??");
	return -1;
    }

    char* uvalue = PyString_AsString(putf8);
    string key = rclconfig->fieldCanon(string(name));

    LOGDEB0(("Doc_setattr: [%s] (%s) -> [%s]\n", key.c_str(), name, uvalue));
    // We set the value in the meta array in all cases. Good idea ? or do it
    // only for fields without a dedicated Doc:: entry?
    self->doc->meta[key] = uvalue;
    switch (key.at(0)) {
    case 't':
	if (!key.compare("text")) {
	    self->doc->text = uvalue;
	}
	break;
    case 'u':
	if (!key.compare(Rcl::Doc::keyurl)) {
	    self->doc->url = uvalue;
	}
	break;
    case 'f':
	if (!key.compare(Rcl::Doc::keyfs)) {
	    self->doc->fbytes = uvalue;
	} else if (!key.compare(Rcl::Doc::keyfmt)) {
	    self->doc->fmtime = uvalue;
	}
	break;
    case 'd':
	if (!key.compare(Rcl::Doc::keyds)) {
	    self->doc->dbytes = uvalue;
	} else if (!key.compare(Rcl::Doc::keydmt)) {
	    self->doc->dmtime = uvalue;
	}
	break;
    case 'i':
	if (!key.compare(Rcl::Doc::keyipt)) {
	    self->doc->ipath = uvalue;
	}
	break;
    case 'm':
	if (!key.compare(Rcl::Doc::keytp)) {
	    self->doc->mimetype = uvalue;
	} else if (!key.compare(Rcl::Doc::keymt)) {
	    self->doc->dmtime = uvalue;
	}
	break;
    case 'o':
	if (!key.compare(Rcl::Doc::keyoc)) {
	    self->doc->origcharset = uvalue;
	}
	break;
    case 's':
	if (!key.compare(Rcl::Doc::keysig)) {
	    self->doc->sig = uvalue;
	} else 	if (!key.compare(Rcl::Doc::keysz)) {
	    self->doc->dbytes = uvalue;
	}
	break;
    }
    return 0;
}


PyDoc_STRVAR(doc_DocObject,
"Doc()\n"
"\n"
"A Doc object contains index data for a given document.\n"
"The data is extracted from the index when searching, or set by the\n"
"indexer program when updating. The Doc object has no useful methods but\n"
"many attributes to be read or set by its user. It matches exactly the\n"
"Rcl::Doc c++ object. Some of the attributes are predefined, but, \n"
"especially when indexing, others can be set, the name of which will be\n"
"processed as field names by the indexing configuration.\n"
"Inputs can be specified as unicode or strings.\n"
"Outputs are unicode objects.\n"
"All dates are specified as unix timestamps, printed as strings\n"
"Predefined attributes (index/query/both):\n"
" text (index): document plain text\n"
" url (both)\n"
" fbytes (both) optional) file size in bytes\n"
" filename (both)\n"
" fmtime (both) optional file modification date. Unix time printed \n"
"    as string\n"
" dbytes (both) document text bytes\n"
" dmtime (both) document creation/modification date\n"
" ipath (both) value private to the app.: internal access path\n"
"    inside file\n"
" mtype (both) mime type for original document\n"
" mtime (query) dmtime if set else fmtime\n"
" origcharset (both) charset the text was converted from\n"
" size (query) dbytes if set, else fbytes\n"
" sig (both) app-defined file modification signature. \n"
"    For up to date checks\n"
" relevancyrating (query)\n"
" abstract (both)\n"
" author (both)\n"
" title (both)\n"
" keywords (both)\n"
);
static PyTypeObject recoll_DocType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "recoll.Doc",             /*tp_name*/
    sizeof(recoll_DocObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Doc_dealloc,    /*tp_dealloc*/
    0,                         /*tp_print*/
    (getattrfunc)Doc_getattr,  /*tp_getattr*/
    (setattrfunc)Doc_setattr, /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    doc_DocObject,             /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    Doc_methods,               /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Doc_init,        /* tp_init */
    0,                         /* tp_alloc */
    Doc_new,                   /* tp_new */
};


//////////////////////////////////////////////////////
/// QUERY Query object 

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    Rcl::Query *query;
    int         next; // Index of result to be fetched next or -1 if uninit
    char       *sortfield;
    int         ascending;
} recoll_QueryObject;

static void 
Query_dealloc(recoll_QueryObject *self)
{
    LOGDEB(("Query_dealloc\n"));
    if (self->query)
	the_queries.erase(self->query);
    delete self->query;
    self->query = 0;
    self->sortfield = 0;
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
Query_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    recoll_QueryObject *self;
    LOGDEB(("Query_new\n"));

    self = (recoll_QueryObject *)type->tp_alloc(type, 0);
    if (self == 0) 
	return 0;
    self->query = 0;
    self->next = -1;
    self->sortfield = 0;
    return (PyObject *)self;
}

// Query_init creates an unusable object. The only way to create a
// valid Query Object is through db_query(). (or we'd need to add a Db
// parameter to the Query object creation method)
static int
Query_init(recoll_QueryObject *self, PyObject *, PyObject *)
{
    LOGDEB(("Query_init\n"));

    if (self->query)
	the_queries.erase(self->query);
    delete self->query;
    self->query = 0;
    self->next = -1;
    self->sortfield = 0;
    self->ascending = true;
    return 0;
}

PyDoc_STRVAR(doc_Query_sortby,
"sortby(field=fieldname, ascending=true)\n"
"Sort results by 'fieldname', in ascending or descending order.\n"
"Only one field can be used, no subsorts for now.\n"
"Must be called before executing the search\n"
);

static PyObject *
Query_sortby(recoll_QueryObject* self, PyObject *args, PyObject *kwargs)
{
    LOGDEB(("Query_sortby\n"));
    static const char *kwlist[] = {"field", "ascending", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|i", (char**)kwlist,
				     &self->sortfield,
				     &self->ascending))
	return 0;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(doc_Query_execute,
"execute(query_string, stemming=1|0)\n"
"\n"
"Starts a search for query_string, a Recoll search language string\n"
"(mostly Xesam-compatible).\n"
"The query can be a simple list of terms (and'ed by default), or more\n"
"complicated with field specs etc. See the Recoll manual.\n"
);

static PyObject *
Query_execute(recoll_QueryObject* self, PyObject *args, PyObject *kwargs)
{
    LOGDEB(("Query_execute\n"));
    static const char *kwlist[] = {"query_string", "stemming", NULL};
    char *sutf8 = 0; // needs freeing
    int dostem = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "es|i:Query_execute", 
				     (char**)kwlist, "utf-8", &sutf8,
				     &dostem)) {
	return 0;
    }
    LOGDEB(("Query_execute:  [%s]\n", sutf8));

    string utf8(sutf8);
    PyMem_Free(sutf8);
    if (self->query == 0 || 
	the_queries.find(self->query) == the_queries.end()) {
        PyErr_SetString(PyExc_AttributeError, "query");
	return 0;
    }

    // SearchData defaults to stemming in english
    // Use default for now but need to add way to specify language
    string reason;
    Rcl::SearchData *sd = wasaStringToRcl(rclconfig, dostem ? "english" : "",
					  utf8, reason);

    if (!sd) {
	PyErr_SetString(PyExc_ValueError, reason.c_str());
	return 0;
    }

    RefCntr<Rcl::SearchData> rq(sd);
    string sf = self->sortfield ? string(self->sortfield) : string("");
    self->query->setSortBy(sf, self->ascending);
    self->query->setQuery(rq);
    int cnt = self->query->getResCnt();
    self->next = 0;
    return Py_BuildValue("i", cnt);
}

PyDoc_STRVAR(doc_Query_executesd,
"executesd(SearchData)\n"
"\n"
"Starts a search for the query defined by the SearchData object.\n"
);

static PyObject *
Query_executesd(recoll_QueryObject* self, PyObject *args, PyObject *kwargs)
{
    static const char *kwlist[] = {"searchdata", NULL};
    recoll_SearchDataObject *pysd = 0;
    LOGDEB(("Query_executeSD\n"));
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!:Query_execute", 
				     (char **)kwlist,
				     &recoll_SearchDataType, &pysd)) {
	return 0;
    }
    if (pysd == 0 || self->query == 0 || 
	the_queries.find(self->query) == the_queries.end()) {
        PyErr_SetString(PyExc_AttributeError, "query");
	return 0;
    }
    string sf = self->sortfield ? string(self->sortfield) : string("");
    self->query->setSortBy(sf, self->ascending);
    self->query->setQuery(pysd->sd);
    int cnt = self->query->getResCnt();
    self->next = 0;
    return Py_BuildValue("i", cnt);
}

PyDoc_STRVAR(doc_Query_fetchone,
"fetchone(None) -> Doc\n"
"\n"
"Fetches the next Doc object in the current search results.\n"
);
static PyObject *
Query_fetchone(recoll_QueryObject* self, PyObject *, PyObject *)
{
    LOGDEB(("Query_fetchone\n"));

    if (self->query == 0 || 
	the_queries.find(self->query) == the_queries.end()) {
        PyErr_SetString(PyExc_AttributeError, "query");
	return 0;
    }
    int cnt = self->query->getResCnt();
    if (cnt <= 0 || self->next < 0) {
        PyErr_SetString(PyExc_AttributeError, "query: no results");
	return 0;
    }
    recoll_DocObject *result = 
	(recoll_DocObject *)obj_Create(&recoll_DocType, 0, 0);
    if (!result) {
	LOGERR(("Query_fetchone: couldn't create doc object for result\n"));
	return 0;
    }
    if (!self->query->getDoc(self->next, *result->doc)) {
        PyErr_SetString(PyExc_EnvironmentError, "query: cant fetch result");
	self->next = -1;
	return 0;
    }
    self->next++;

    // Move some data from the dedicated fields to the meta array to make 
    // fetching attributes easier. Is this actually needed ? Useful for
    // url which is also formatted .
    Rcl::Doc *doc = result->doc;
    printableUrl(rclconfig->getDefCharset(), doc->url, 
		 doc->meta[Rcl::Doc::keyurl]);
    doc->meta[Rcl::Doc::keytp] = doc->mimetype;
    doc->meta[Rcl::Doc::keyipt] = doc->ipath;
    doc->meta[Rcl::Doc::keyfs] = doc->fbytes;
    doc->meta[Rcl::Doc::keyds] = doc->dbytes;
    return (PyObject *)result;
}

static PyMethodDef Query_methods[] = {
    {"execute", (PyCFunction)Query_execute, METH_VARARGS|METH_KEYWORDS, 
     doc_Query_execute},
    {"executesd", (PyCFunction)Query_executesd, METH_VARARGS|METH_KEYWORDS, 
     doc_Query_executesd},
    {"fetchone", (PyCFunction)Query_fetchone, METH_VARARGS,
     doc_Query_fetchone},
    {"sortby", (PyCFunction)Query_sortby, METH_VARARGS|METH_KEYWORDS,
     doc_Query_sortby},
    {NULL}  /* Sentinel */
};

static PyMemberDef Query_members[] = {
    {(char*)"next", T_INT, offsetof(recoll_QueryObject, next), 0,
     (char*)"Next index to be fetched from results. Normally increments after\n"
     "each fetchone() call, but can be set/reset before the call effect\n"
     "seeking. Starts at 0"
    },
    {NULL}  /* Sentinel */
};

PyDoc_STRVAR(doc_QueryObject,
"Recoll Query objects are used to execute index searches. \n"
"They must be created by the Db.query() method.\n"
	     );
static PyTypeObject recoll_QueryType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "recoll.Query",             /*tp_name*/
    sizeof(recoll_QueryObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Query_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    doc_QueryObject,           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    Query_methods,             /* tp_methods */
    Query_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Query_init,      /* tp_init */
    0,                         /* tp_alloc */
    Query_new,                 /* tp_new */
};


///////////////////////////////////////////////
////// DB Db object code
typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    Rcl::Db *db;
} recoll_DbObject;

static void 
Db_dealloc(recoll_DbObject *self)
{
    LOGDEB(("Db_dealloc\n"));
    if (self->db)
	the_dbs.erase(self->db);
    delete self->db;
    self->db = 0;
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
Db_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    LOGDEB(("Db_new\n"));
    recoll_DbObject *self;

    self = (recoll_DbObject *)type->tp_alloc(type, 0);
    if (self == 0) 
	return 0;
    self->db = 0;
    return (PyObject *)self;
}

static int
Db_init(recoll_DbObject *self, PyObject *args, PyObject *kwargs)
{
    LOGDEB(("Db_init\n"));
    static const char *kwlist[] = {"confdir", "extra_dbs", "writable", NULL};
    PyObject *extradbs = 0;
    char *confdir = 0;
    int writable = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|sOi", (char**)kwlist,
				     &confdir, &extradbs, &writable))
	return -1;

    // If the user creates several dbs, changing the confdir, we call 
    // recollinit repeatedly, which *should* be ok...
    string reason;
    delete rclconfig;
    if (confdir) {
	string cfd = confdir;
	rclconfig = recollinit(0, 0, reason, &cfd);
    } else {
	rclconfig = recollinit(0, 0, reason, 0);
    }
    if (rclconfig == 0) {
	PyErr_SetString(PyExc_EnvironmentError, reason.c_str());
	return -1;
    }
    if (!rclconfig->ok()) {
	PyErr_SetString(PyExc_EnvironmentError, "Bad config ?");
	return -1;
    }

    if (self->db)
	the_dbs.erase(self->db);
    delete self->db;
    self->db = new Rcl::Db(rclconfig);
    if (!self->db->open(writable ? Rcl::Db::DbUpd : Rcl::Db::DbRO)) {
	LOGERR(("Db_init: db open error\n"));
	PyErr_SetString(PyExc_EnvironmentError, "Can't open index");
        return -1;
    }

    if (extradbs) {
	if (!PySequence_Check(extradbs)) {
	    PyErr_SetString(PyExc_TypeError, "extra_dbs must be a sequence");
	    deleteZ(self->db);
	    return -1;
	}
	int dbcnt = PySequence_Size(extradbs);
	if (dbcnt == -1) {
	    PyErr_SetString(PyExc_TypeError, "extra_dbs could not be sized");
	    deleteZ(self->db);
	    return -1;
	}
	for (int i = 0; i < dbcnt; i++) {
	    PyObject *item = PySequence_GetItem(extradbs, i);
	    char *s = PyString_AsString(item);
	    Py_DECREF(item);
	    if (!s) {
		PyErr_SetString(PyExc_TypeError,
				"extra_dbs must contain strings");
		deleteZ(self->db);
		return -1;
	    }
	    if (!self->db->addQueryDb((const char *)s)) {
		PyErr_SetString(PyExc_EnvironmentError, 
				"extra db could not be opened");
		deleteZ(self->db);
		return -1;
	    }
	}
    }

    the_dbs.insert(self->db);
    return 0;
}

static PyObject *
Db_query(recoll_DbObject* self)
{
    LOGDEB(("Db_query\n"));
    if (self->db == 0 || the_dbs.find(self->db) == the_dbs.end()) {
	LOGERR(("Db_query: db not found %p\n", self->db));
        PyErr_SetString(PyExc_AttributeError, "db");
        return 0;
    }
    recoll_QueryObject *result = 
	(recoll_QueryObject *)obj_Create(&recoll_QueryType, 0, 0);
    if (!result)
	return 0;
    result->query = new Rcl::Query(self->db);
    the_queries.insert(result->query);
    return (PyObject *)result;
}

static PyObject *
Db_setAbstractParams(recoll_DbObject *self, PyObject *args, PyObject *kwargs)
{
    LOGDEB(("Db_setAbstractParams\n"));
    static const char *kwlist[] = {"maxchars", "contextwords", NULL};
    int ctxwords = -1, maxchars = -1;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ii", (char**)kwlist,
				     &maxchars, &ctxwords))
	return 0;
    if (self->db == 0 || the_dbs.find(self->db) == the_dbs.end()) {
	LOGERR(("Db_query: db not found %p\n", self->db));
        PyErr_SetString(PyExc_AttributeError, "db id not found");
        return 0;
    }
    self->db->setAbstractParams(-1, maxchars, ctxwords);
    Py_RETURN_NONE;
}

static PyObject *
Db_makeDocAbstract(recoll_DbObject* self, PyObject *args, PyObject *)
{
    LOGDEB(("Db_makeDocAbstract\n"));
    recoll_DocObject *pydoc = 0;
    recoll_QueryObject *pyquery = 0;
    if (!PyArg_ParseTuple(args, "O!O!:Db_makeDocAbstract",
			  &recoll_DocType, &pydoc,
			  &recoll_QueryType, &pyquery)) {
	return 0;
    }
    if (self->db == 0 || the_dbs.find(self->db) == the_dbs.end()) {
	LOGERR(("Db_makeDocAbstract: db not found %p\n", self->db));
        PyErr_SetString(PyExc_AttributeError, "db");
        return 0;
    }
    if (pydoc->doc == 0 || the_docs.find(pydoc->doc) == the_docs.end()) {
	LOGERR(("Db_makeDocAbstract: doc not found %p\n", pydoc->doc));
        PyErr_SetString(PyExc_AttributeError, "doc");
        return 0;
    }
    if (pyquery->query == 0 || 
	the_queries.find(pyquery->query) == the_queries.end()) {
	LOGERR(("Db_makeDocAbstract: query not found %p\n", pyquery->query));
        PyErr_SetString(PyExc_AttributeError, "query");
        return 0;
    }
    string abstract;
    if (!self->db->makeDocAbstract(*(pydoc->doc), pyquery->query, abstract)) {
	PyErr_SetString(PyExc_EnvironmentError, "rcl makeDocAbstract failed");
        return 0;
    }
    // Return a python unicode object
    return PyUnicode_Decode(abstract.c_str(), abstract.size(), 
				     "UTF-8", "replace");
}

static PyObject *
Db_needUpdate(recoll_DbObject* self, PyObject *args, PyObject *kwds)
{
    char *udi = 0; // needs freeing
    char *sig = 0; // needs freeing
    LOGDEB(("Db_needUpdate\n"));
    if (!PyArg_ParseTuple(args, "eses:Db_needUpdate", 
			  "utf-8", &udi, "utf-8", &sig)) {
	return 0;
    }
    if (self->db == 0 || the_dbs.find(self->db) == the_dbs.end()) {
	LOGERR(("Db_needUpdate: db not found %p\n", self->db));
        PyErr_SetString(PyExc_AttributeError, "db");
	PyMem_Free(udi);
	PyMem_Free(sig);
        return 0;
    }
    bool result = self->db->needUpdate(udi, sig);
    PyMem_Free(udi);
    PyMem_Free(sig);
    return Py_BuildValue("i", result);
}

static PyObject *
Db_delete(recoll_DbObject* self, PyObject *args, PyObject *kwds)
{
    char *udi = 0; // needs freeing
    LOGDEB(("Db_delete\n"));
    if (!PyArg_ParseTuple(args, "es:Db_delete", "utf-8", &udi)) {
	return 0;
    }
    if (self->db == 0 || the_dbs.find(self->db) == the_dbs.end()) {
	LOGERR(("Db_delete: db not found %p\n", self->db));
        PyErr_SetString(PyExc_AttributeError, "db");
	PyMem_Free(udi);
        return 0;
    }
    bool result = self->db->purgeFile(udi);
    PyMem_Free(udi);
    return Py_BuildValue("i", result);
}

static PyObject *
Db_purge(recoll_DbObject* self)
{
    if (self->db == 0 || the_dbs.find(self->db) == the_dbs.end()) {
	LOGERR(("Db_purge: db not found %p\n", self->db));
        PyErr_SetString(PyExc_AttributeError, "db");
        return 0;
    }
    bool result = self->db->purge();
    return Py_BuildValue("i", result);
}

static PyObject *
Db_addOrUpdate(recoll_DbObject* self, PyObject *args, PyObject *)
{
    LOGDEB(("Db_addOrUpdate\n"));
    char *sudi = 0; // needs freeing
    char *sparent_udi = 0; // needs freeing
    recoll_DocObject *pydoc;

    if (!PyArg_ParseTuple(args, "esO!|es:Db_addOrUpdate",
			  "utf-8", &sudi, &recoll_DocType, &pydoc,
			  "utf-8", &sparent_udi)) {
	return 0;
    }
    string udi(sudi);
    string parent_udi(sparent_udi ? sparent_udi : "");
    PyMem_Free(sudi);
    PyMem_Free(sparent_udi);

    if (self->db == 0 || the_dbs.find(self->db) == the_dbs.end()) {
	LOGERR(("Db_addOrUpdate: db not found %p\n", self->db));
        PyErr_SetString(PyExc_AttributeError, "db");
        return 0;
    }
    if (pydoc->doc == 0 || the_docs.find(pydoc->doc) == the_docs.end()) {
	LOGERR(("Db_addOrUpdate: doc not found %p\n", pydoc->doc));
        PyErr_SetString(PyExc_AttributeError, "doc");
        return 0;
    }
    if (!self->db->addOrUpdate(udi, parent_udi, *pydoc->doc)) {
	LOGERR(("Db_addOrUpdate: rcldb error\n"));
        PyErr_SetString(PyExc_AttributeError, "rcldb error");
        return 0;
    }
    Py_RETURN_NONE;
}
    
static PyMethodDef Db_methods[] = {
    {"query", (PyCFunction)Db_query, METH_NOARGS,
     "query() -> Query. Return a new, blank query object for this index."
    },
    {"setAbstractParams", (PyCFunction)Db_setAbstractParams, 
     METH_VARARGS|METH_KEYWORDS,
     "setAbstractParams(maxchars, contextwords).\n"
     "Set the parameters used to build 'keyword-in-context' abstracts"
    },
    {"makeDocAbstract", (PyCFunction)Db_makeDocAbstract, METH_VARARGS,
     "makeDocAbstract(Doc, Query) -> string\n"
     "Build and return 'keyword-in-context' abstract for document\n"
     "and query."
    },
    {"needUpdate", (PyCFunction)Db_needUpdate, METH_VARARGS,
     "needUpdate(udi, sig) -> Bool.\n"
     "Check if the index is up to date for the document defined by udi,\n"
     "having the current signature sig."
    },
    {"delete", (PyCFunction)Db_delete, METH_VARARGS,
     "delete(udi) -> Bool.\n"
     "Purge index from all data for udi. If udi matches a container\n"
     "document, purge all subdocs (docs with a parent_udi matching udi)."
    },
    {"purge", (PyCFunction)Db_purge, METH_NOARGS,
     "purge() -> Bool.\n"
     "Delete all documents that were not touched during the just finished\n"
     "indexing pass (since open-for-write). These are the documents for\n"
     "the needUpdate() call was not performed, indicating that they no\n"
     "longer exist in the primary storage system.\n"
    },
    {"addOrUpdate", (PyCFunction)Db_addOrUpdate, METH_VARARGS,
     "addOrUpdate(udi, doc, parent_udi=None) -> None\n"
     "Add or update index data for a given document\n"
     "The udi string must define a unique id for the document. It is not\n"
     "interpreted inside Recoll\n"
     "doc is a Doc object\n"
     "if parent_udi is set, this is a unique identifier for the\n"
     "top-level container (ie mbox file)"
    },
    {NULL}  /* Sentinel */
};
PyDoc_STRVAR(doc_DbObject,
"Db([confdir=None], [extra_dbs=None], [writable = False])\n"
"\n"
"A Db object holds a connection to a Recoll index. Use the connect()\n"
"function to create one.\n"
"confdir specifies a Recoll configuration directory (default: \n"
" $RECOLL_CONFDIR or ~/.recoll).\n"
"extra_dbs is a list of external databases (xapian directories)\n"
"writable decides if we can index new data through this connection\n"
);
static PyTypeObject recoll_DbType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "recoll.Db",             /*tp_name*/
    sizeof(recoll_DbObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Db_dealloc,    /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    doc_DbObject,              /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    Db_methods,                /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Db_init,         /* tp_init */
    0,                         /* tp_alloc */
    Db_new,                    /* tp_new */
};


//////////////////////////////////////////////////////////////////////////
// Module methods
static PyObject *
recoll_connect(PyObject *self, PyObject *args, PyObject *kwargs)
{
    LOGDEB(("recoll_connect\n"));
    recoll_DbObject *db = 
	(recoll_DbObject *)obj_Create(&recoll_DbType, args, kwargs);

    return (PyObject *)db;
}

PyDoc_STRVAR(doc_connect,
"connect([confdir=None], [extra_dbs=None], [writable = False])\n"
"         -> Db.\n"
"\n"
"Connects to a Recoll database and returns a Db object.\n"
"confdir specifies a Recoll configuration directory\n"
"(the default is built like for any Recoll program).\n"
"extra_dbs is a list of external databases (xapian directories)\n"
"writable decides if we can index new data through this connection\n"
);

static PyMethodDef recollMethods[] = {
    {"connect",  (PyCFunction)recoll_connect, METH_VARARGS|METH_KEYWORDS, 
     doc_connect},

    {NULL, NULL, 0, NULL}        /* Sentinel */
};


PyDoc_STRVAR(pyrecoll_doc_string,
"This is an interface to the Recoll full text indexer.");

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
initrecoll(void)
{
    string reason;
    rclconfig = recollinit(0, 0, reason, 0);
    if (rclconfig == 0) {
	PyErr_SetString(PyExc_EnvironmentError, reason.c_str());
	return;
    }
    if (!rclconfig->ok()) {
	PyErr_SetString(PyExc_EnvironmentError, 
			"Recoll init error: bad environment ?");
	return;
    }

    PyObject* m;
    m = Py_InitModule3("recoll", recollMethods,
                       "Recoll extension module.");
    
    if (PyType_Ready(&recoll_DbType) < 0)
        return;
    Py_INCREF(&recoll_DbType);
    PyModule_AddObject(m, "Db", (PyObject *)&recoll_DbType);

    if (PyType_Ready(&recoll_QueryType) < 0)
        return;
    Py_INCREF(&recoll_QueryType);
    PyModule_AddObject(m, "Query", (PyObject *)&recoll_QueryType);

    if (PyType_Ready(&recoll_DocType) < 0)
        return;
    Py_INCREF(&recoll_DocType);
    PyModule_AddObject(m, "Doc", (PyObject *)&recoll_DocType);

    if (PyType_Ready(&recoll_SearchDataType) < 0)
        return;
    Py_INCREF(&recoll_SearchDataType);
    PyModule_AddObject(m, "SearchData", (PyObject *)&recoll_SearchDataType);
    PyModule_AddStringConstant(m, "__doc__",
                               pyrecoll_doc_string);

}
