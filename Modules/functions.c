/* See https://www.python-ldap.org/ for details. */

#include "common.h"
#include "functions.h"
#include "LDAPObject.h"
#include "berval.h"
#include "constants.h"
#include "options.h"

/* ldap_initialize */

static PyObject *
l_ldap_initialize(PyObject *unused, PyObject *args)
{
    char *uri;
    LDAP *ld = NULL;
    int ret;
    PyThreadState *save;

    if (!PyArg_ParseTuple(args, "s:initialize", &uri))
        return NULL;

    save = PyEval_SaveThread();
    ret = ldap_initialize(&ld, uri);
    PyEval_RestoreThread(save);

    if (ret != LDAP_SUCCESS)
        return LDAPerror(ld);

    return (PyObject *)newLDAPObject(ld);
}

#ifdef HAVE_LDAP_INIT_FD
/* initialize_fd(fileno, url) */

static PyObject *
l_ldap_initialize_fd(PyObject *unused, PyObject *args)
{
    char *url;
    LDAP *ld = NULL;
    int ret;
    int fd;
    int proto = -1;
    LDAPURLDesc *lud = NULL;

    PyThreadState *save;

    if (!PyArg_ParseTuple(args, "is:initialize_fd", &fd, &url))
        return NULL;

    /* Get LDAP protocol from scheme */
    ret = ldap_url_parse(url, &lud);
    if (ret != LDAP_SUCCESS)
        return LDAPerr(ret);

    if (strcmp(lud->lud_scheme, "ldap") == 0) {
        proto = LDAP_PROTO_TCP;
    }
    else if (strcmp(lud->lud_scheme, "ldaps") == 0) {
        proto = LDAP_PROTO_TCP;
    }
    else if (strcmp(lud->lud_scheme, "ldapi") == 0) {
        proto = LDAP_PROTO_IPC;
    }
#ifdef LDAP_CONNECTIONLESS
    else if (strcmp(lud->lud_scheme, "cldap") == 0) {
        proto = LDAP_PROTO_UDP;
    }
#endif
    else {
        ldap_free_urldesc(lud);
        PyErr_SetString(PyExc_ValueError, "unsupported URL scheme");
        return NULL;
    }
    ldap_free_urldesc(lud);

    save = PyEval_SaveThread();
    ret = ldap_init_fd((ber_socket_t) fd, proto, url, &ld);
    PyEval_RestoreThread(save);

    if (ret != LDAP_SUCCESS)
        return LDAPerror(ld);

    return (PyObject *)newLDAPObject(ld);
}
#endif

/* ldap_str2dn */

static PyObject *
l_ldap_str2dn(PyObject *unused, PyObject *args)
{
    struct berval str;
    LDAPDN dn;
    int flags = 0;
    PyObject *result = NULL, *tmp;
    int res, i, j;
    Py_ssize_t str_len;

    /*
     * From a DN string such as "a=b,c=d;e=f", build
     * a list-equivalent of AVA structures; namely:
     * ((('a','b',1),('c','d',1)),(('e','f',1),))
     * The integers are a bit combination of the AVA_* flags
     */
    if (!PyArg_ParseTuple(args, "z#|i:str2dn", &str.bv_val, &str_len, &flags))
        return NULL;
    str.bv_len = (ber_len_t) str_len;

    res = ldap_bv2dn(&str, &dn, flags);
    if (res != LDAP_SUCCESS)
        return LDAPerr(res);

    tmp = PyList_New(0);
    if (!tmp)
        goto failed;

    for (i = 0; dn[i]; i++) {
        LDAPRDN rdn;
        PyObject *rdnlist;

        rdn = dn[i];
        rdnlist = PyList_New(0);
        if (!rdnlist)
            goto failed;
        if (PyList_Append(tmp, rdnlist) == -1) {
            Py_DECREF(rdnlist);
            goto failed;
        }

        for (j = 0; rdn[j]; j++) {
            LDAPAVA *ava = rdn[j];
            PyObject *tuple;

            tuple = Py_BuildValue("(O&O&i)",
                                  LDAPberval_to_unicode_object, &ava->la_attr,
                                  LDAPberval_to_unicode_object, &ava->la_value,
                                  ava->la_flags & ~(LDAP_AVA_FREE_ATTR |
                                                    LDAP_AVA_FREE_VALUE));
            if (!tuple) {
                Py_DECREF(rdnlist);
                goto failed;
            }

            if (PyList_Append(rdnlist, tuple) == -1) {
                Py_DECREF(tuple);
                goto failed;
            }
            Py_DECREF(tuple);
        }
        Py_DECREF(rdnlist);
    }

    result = tmp;
    tmp = NULL;

  failed:
    Py_XDECREF(tmp);
    ldap_dnfree(dn);
    return result;
}

/* ldap_dn2str */

static PyObject *
l_ldap_dn2str(PyObject *unused, PyObject *args)
{
    struct berval str;
    LDAPDN dn = malloc(sizeof(LDAPDN));
    int flags = 0;
    PyObject *result = NULL, *tmp, *dn_list;
    int res, i, j;
    char *type_error_message = "expected List[Tuple[str, str, int]]";

    /*
     * From a list-equivalent of AVA structures; namely:
     * ((('a', 'b', 1), ('c', 'd', 1)), (('e', 'f', 1),)), build
     * a DN string such as "a=b,c=d;e=f".
     * The integers are a bit combination of the AVA_* flags
     */
    if (!PyArg_ParseTuple(args, "Oi:dn2str", &dn_list, &flags))
        return NULL;

    PyObject *iter = PyObject_GetIter(dn_list);
    if (!iter) {
        // TypeError
        PyErr_SetString(PyExc_TypeError, type_error_message);
        goto failed;
    }

    i = 0;
    while (1) {
        PyObject *inext = PyIter_Next(iter);
        if (!inext) {
            break;
        }

        if (PyList_Check(inext)) {
            inext = PyList_AsTuple(inext);
        }
        if (!PyTuple_Check(inext)) {
            PyErr_SetString(PyExc_TypeError, type_error_message);
            goto failed;
        }

        PyObject *iiter = PyObject_GetIter(inext);

        j = 0;
        LDAPRDN rdn = malloc(sizeof(LDAPRDN));
        while (1) {
            PyObject *next = PyIter_Next(iiter);
            if (!next) {
                break;
            }

            if (PyList_Check(next)) {
                next = PyList_AsTuple(next);
            }
            if (!PyTuple_Check(next) || PyTuple_Size(next) < 3) {
                PyErr_SetString(PyExc_TypeError, type_error_message);
                goto failed;
            }

            PyObject *type, *value, *encoding, *btype, *bvalue;

            type = PyTuple_GetItem(next, 0);
            value = PyTuple_GetItem(next, 1);
            encoding = PyTuple_GetItem(next, 2);

            if (!PyUnicode_Check(type) || !PyUnicode_Check(value) || !PyLong_Check(encoding)) {
                PyErr_SetString(PyExc_TypeError, type_error_message);
                goto failed;
            }

            btype = PyUnicode_AsEncodedString(type, "utf-8", "strict");
            bvalue = PyUnicode_AsEncodedString(value, "utf-8", "strict");

            struct berval attrType = {
                .bv_val = PyBytes_AsString(btype),
                .bv_len = (int)PyBytes_Size(btype),
            };
            struct berval attrValue = {
                .bv_val = PyBytes_AsString(bvalue),
                .bv_len = (int)PyBytes_Size(bvalue),
            };

            LDAPAVA *ava = malloc(sizeof(LDAPAVA));
            ava->la_attr = attrType;
            ava->la_value = attrValue;
            ava->la_flags = (int)PyLong_AsLong(encoding);

            rdn[j] = ava;
            j++;
        }

        dn[i] = rdn;
        i++;
    }

    res = ldap_dn2bv(dn, &str, flags);
    if (res != LDAP_SUCCESS)
        return LDAPerr(res);  // TODO: no attr set

    tmp = PyUnicode_FromString(str.bv_val);
    if (!tmp)
        goto failed;

    result = tmp;
    tmp = NULL;

  failed:
    Py_XDECREF(tmp);
    // ldap_dnfree(dn);
    return result;
}

/* ldap_set_option (global options) */

static PyObject *
l_ldap_set_option(PyObject *self, PyObject *args)
{
    PyObject *value;
    int option;

    if (!PyArg_ParseTuple(args, "iO:set_option", &option, &value))
        return NULL;
    if (!LDAP_set_option(NULL, option, value))
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

/* ldap_get_option (global options) */

static PyObject *
l_ldap_get_option(PyObject *self, PyObject *args)
{
    int option;

    if (!PyArg_ParseTuple(args, "i:get_option", &option))
        return NULL;
    return LDAP_get_option(NULL, option);
}

/* methods */

static PyMethodDef methods[] = {
    {"initialize", (PyCFunction)l_ldap_initialize, METH_VARARGS},
#ifdef HAVE_LDAP_INIT_FD
    {"initialize_fd", (PyCFunction)l_ldap_initialize_fd, METH_VARARGS},
#endif
    {"str2dn", (PyCFunction)l_ldap_str2dn, METH_VARARGS},
    {"dn2str", (PyCFunction)l_ldap_dn2str, METH_VARARGS},
    {"set_option", (PyCFunction)l_ldap_set_option, METH_VARARGS},
    {"get_option", (PyCFunction)l_ldap_get_option, METH_VARARGS},
    {NULL, NULL}
};

/* initialisation */

void
LDAPinit_functions(PyObject *d)
{
    LDAPadd_methods(d, methods);
}
