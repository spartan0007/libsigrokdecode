/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"
#include <sigrokdecode.h> /* First, so we avoid a _POSIX_C_SOURCE warning. */
#include <stdio.h>
#include <string.h>
#include <dirent.h>

/* Re-define some string functions for Python >= 3.0. */
#if PY_VERSION_HEX >= 0x03000000
#define PyString_AsString PyBytes_AsString
#define PyString_FromString PyBytes_FromString
#define PyString_Check PyBytes_Check
#endif

/* The list of protocol decoders. */
static GSList *list_pds = NULL;
/* The list of protocol decoder instances: struct srd_decoder_instance */
static GSList *decoders;

/*
 * Here's a quick overview of Python/C API reference counting.
 *
 * Check the Python/C API docs for what type of reference a function returns.
 *
 *  - If it returns a "new reference", you're responsible to Py_XDECREF() it.
 *
 *  - If it returns a "borrowed reference", you MUST NOT Py_XDECREF() it.
 *
 *  - If a function "steals" a reference, you no longer are responsible for
 *    Py_XDECREF()ing it (someone else will do it for you at some point).
 */

static int srd_load_decoder(PyObject *py_res);

static PyObject *emb_register(PyObject *self, PyObject *args)
{
	PyObject *arg;

	(void)self;

	if (!PyArg_ParseTuple(args, "O:decoder", &arg))
		return NULL;

	srd_load_decoder(arg);

	Py_RETURN_NONE;
}

static PyObject *emb_put(PyObject *self, PyObject *args)
{
	PyObject *arg;

	(void)self;

	if (!PyArg_ParseTuple(args, "O:put", &arg))
		return NULL;

	PyObject_Print(arg, stdout, Py_PRINT_RAW);
	puts("");

	Py_RETURN_NONE;
}

static PyMethodDef EmbMethods[] = {
	{"register", emb_register, METH_VARARGS,
	 "Register a protocol decoder object with libsigrokdecode."},
	{"put", emb_put, METH_VARARGS,
	 "Accepts a dictionary with the following keys: time, duration, data"},
	{NULL, NULL, 0, NULL}
};

/**
 * Initialize libsigrokdecode.
 *
 * This initializes the Python interpreter, and creates and initializes
 * a "sigrok" Python module with a single put() method.
 *
 * Then, it searches for sigrok protocol decoder files (*.py) in the
 * "decoders" subdirectory of the the sigrok installation directory.
 * All decoders that are found are loaded into memory and added to an
 * internal list of decoders, which can be queried via srd_list_decoders().
 *
 * The caller is responsible for calling the clean-up function srd_exit(),
 * which will properly shut down libsigrokdecode and free its allocated memory.
 *
 * Multiple calls to srd_init(), without calling srd_exit() inbetween,
 * are not allowed.
 *
 * @return SRD_OK upon success, a (negative) error code otherwise.
 *         Upon Python errors, return SRD_ERR_PYTHON. If the sigrok decoders
 *         directory cannot be accessed, return SRD_ERR_DECODERS_DIR.
 *         If not enough memory could be allocated, return SRD_ERR_MALLOC.
 */
int srd_init(void)
{
	DIR *dir;
	struct dirent *dp;
	char *decodername;
	struct srd_decoder *dec;
	int ret;

	/* Py_Initialize() returns void and usually cannot fail. */
	Py_Initialize();

	/* TODO: Use Py_InitModule3() to add a docstring? */
	if (!Py_InitModule("sigrok", EmbMethods)) {
		Py_Finalize(); /* Returns void. */
		return SRD_ERR_PYTHON;
	}

	/* Add search directory for the protocol decoders. */
	/* FIXME: What happens if this function is called multiple times? */
	ret = PyRun_SimpleString("import sys;"
				 "sys.path.append(r'" DECODERS_DIR "');");
	if (ret != 0) {
		Py_Finalize(); /* Returns void. */
		return SRD_ERR_PYTHON;
	}

	if (!(dir = opendir(DECODERS_DIR))) {
		Py_Finalize(); /* Returns void. */
		return SRD_ERR_DECODERS_DIR;
	}

	while ((dp = readdir(dir)) != NULL) {
		/* Ignore filenames which don't end with ".py". */
		if (!g_str_has_suffix(dp->d_name, ".py"))
			continue;

		/* Decoder name == filename (without .py suffix). */
		decodername = g_strndup(dp->d_name, strlen(dp->d_name) - 3);

		/* TODO: Error handling. Use g_try_malloc(). */
		if (!(dec = malloc(sizeof(struct srd_decoder)))) {
			Py_Finalize(); /* Returns void. */
			return SRD_ERR_MALLOC;
		}

		/* Load the decoder. */
		/* "Import" the Python module. */
		PyObject *py_mod;
		if (!(py_mod = PyImport_ImportModule(decodername))) { /* NEWREF */
			PyErr_Print(); /* Returns void. */
			return SRD_ERR_PYTHON; /* TODO: More specific error? */
		}
		/* We release here.  If any decoders were registered they
		 * will hold references. */
		Py_XDECREF(py_mod);
	}
	closedir(dir);

	return SRD_OK;
}

/**
 * Returns the list of supported/loaded protocol decoders.
 *
 * This is a GSList containing the names of the decoders as strings.
 *
 * @return List of decoders, NULL if none are supported or loaded.
 */
GSList *srd_list_decoders(void)
{
	return list_pds;
}

/**
 * Get the decoder with the specified ID.
 *
 * @param id The ID string of the decoder to return.
 * @return The decoder with the specified ID, or NULL if not found.
 */
struct srd_decoder *srd_get_decoder_by_id(const char *id)
{
	GSList *l;
	struct srd_decoder *dec;

	for (l = srd_list_decoders(); l; l = l->next) {
		dec = l->data;
		if (!strcmp(dec->id, id))
			return dec;
	}

	return NULL;
}

/**
 * Helper function to handle Python strings.
 *
 * TODO: @param entries.
 *
 * @return SRD_OK upon success, a (negative) error code otherwise.
 *         The 'outstr' argument points to a malloc()ed string upon success.
 */
static int h_str(PyObject *py_res, const char *key, char **outstr)
{
	PyObject *py_str;
	char *str;
	int ret;

	py_str = PyObject_GetAttrString(py_res, (char *)key); /* NEWREF */
	if (!py_str || !PyString_Check(py_str)) {
		ret = SRD_ERR_PYTHON; /* TODO: More specific error? */
		goto err_h_decref_mod;
	}

	/*
	 * PyString_AsString()'s returned string refers to an internal buffer
	 * (not a copy), i.e. the data must not be modified, and the memory
	 * must not be free()'d.
	 */
	if (!(str = PyString_AsString(py_str))) {
		ret = SRD_ERR_PYTHON; /* TODO: More specific error? */
		goto err_h_decref_str;
	}

	if (!(*outstr = g_strdup(str))) {
		ret = SRD_ERR_MALLOC;
		goto err_h_decref_str;
	}

	Py_XDECREF(py_str);

	return SRD_OK;

err_h_decref_str:
	Py_XDECREF(py_str);
err_h_decref_mod:

	if (PyErr_Occurred())
		PyErr_Print(); /* Returns void. */

	return ret;
}

/**
 * TODO
 *
 * @param name TODO
 *
 * @return SRD_OK upon success, a (negative) error code otherwise.
 */
static int srd_load_decoder(PyObject *py_res)
{
	struct srd_decoder *d;
	int r;

	if (!(d = malloc(sizeof(struct srd_decoder))))
		return SRD_ERR_MALLOC;

	if ((r = h_str(py_res, "id", &(d->id))) < 0)
		return r;

	if ((r = h_str(py_res, "name", &(d->name))) < 0)
		return r;

	if ((r = h_str(py_res, "longname",
		       &(d->longname))) < 0)
		return r;

	if ((r = h_str(py_res, "desc", &(d->desc))) < 0)
		return r;

	if ((r = h_str(py_res, "longdesc",
		       &(d->longdesc))) < 0)
		return r;

	if ((r = h_str(py_res, "author", &(d->author))) < 0)
		return r;

	if ((r = h_str(py_res, "email", &(d->email))) < 0)
		return r;

	if ((r = h_str(py_res, "license", &(d->license))) < 0)
		return r;

	d->py_decobj = py_res;

	/* TODO: Handle func, inputformats, outputformats. */
	/* Note: They must at least be set to NULL, will segfault otherwise. */
	d->func = NULL;
	d->inputformats = NULL;
	d->outputformats = NULL;

	Py_INCREF(py_res);
	fprintf(stderr, "srd: registered '%s'\n", d->id);
	list_pds = g_slist_append(list_pds, d);

	return SRD_OK;
}

/** Create a new decoder instance and add to session. */
struct srd_decoder_instance *srd_instance_new(const char *id)
{
	struct srd_decoder *dec;
	struct srd_decoder_instance *di;
	PyObject *py_args;

	if (!(dec = srd_get_decoder_by_id(id)))
		return NULL;

	/* TODO: Error handling. Use g_try_malloc(). */
	di = g_malloc(sizeof(*di));

	/* Create an empty Python tuple. */
	if (!(py_args = PyTuple_New(0))) { /* NEWREF */
		if (PyErr_Occurred())
			PyErr_Print(); /* Returns void. */

		return NULL; /* TODO: More specific error? */
	}

	/* Create an instance of the 'Decoder' class. */
	di->py_instance = PyObject_Call(dec->py_decobj, py_args, NULL);
	if (!di->py_instance) {
		if (PyErr_Occurred())
			PyErr_Print(); /* Returns void. */
		Py_XDECREF(py_args);
		return NULL; /* TODO: More specific error? */
	}

	/* Append to list of PD instances */
	decoders = g_slist_append(decoders, di);

	Py_XDECREF(py_args);

	return di;
}

int srd_instance_set_probe(struct srd_decoder_instance *di,
			   const char *probename, int num)
{
	PyObject *probedict, *probenum;

	probedict = PyObject_GetAttrString(di->py_instance, "probes"); /* NEWREF */
	if (!probedict) {
		if (PyErr_Occurred())
			PyErr_Print(); /* Returns void. */

		return SRD_ERR_PYTHON; /* TODO: More specific error? */
	}

	probenum = PyInt_FromLong(num);
	PyMapping_SetItemString(probedict, (char *)probename, probenum);

	Py_XDECREF(probenum);
	Py_XDECREF(probedict);

	return SRD_OK;
}

/** Start decoding session.  Feed metadata to decoder instances. */
int srd_session_start(const char *driver, int unitsize, uint64_t starttime,
			uint64_t samplerate)
{
	PyObject *py_res;
	GSList *d;
	for (d = decoders; d; d = d->next) {
		struct srd_decoder_instance *di = d->data;
		/* TODO: Error handling. */
		if (!(py_res = PyObject_CallMethod(di->py_instance, "start",
					"{s:s,s:l,s:l,s:l}", 
					"driver", driver,
					"unitsize", (long)unitsize,
					"starttime", (long)starttime,
					"samplerate", (long)samplerate))) {
			if (PyErr_Occurred())
				PyErr_Print(); /* Returns void. */

			return SRD_ERR_PYTHON; /* TODO: More specific error? */
		}
		Py_XDECREF(py_res);
	}

	return SRD_OK;
}

/**
 * Run the specified decoder function.
 *
 * @param dec TODO
 * @param inbuf TODO
 * @param inbuflen TODO
 * @param outbuf TODO
 * @param outbuflen TODO
 *
 * @return SRD_OK upon success, a (negative) error code otherwise.
 */
static int srd_run_decoder(struct srd_decoder_instance *dec,
		    uint8_t *inbuf, uint64_t inbuflen)
{
	PyObject *py_instance, *py_res;
	/* FIXME: Don't have a timebase available here. Make one up. */
	static int _timehack = 0;

	_timehack += inbuflen;

	/* TODO: Use #defines for the return codes. */

	/* Return an error upon unusable input. */
	if (dec == NULL)
		return SRD_ERR_ARGS; /* TODO: More specific error? */
	if (inbuf == NULL)
		return SRD_ERR_ARGS; /* TODO: More specific error? */
	if (inbuflen == 0) /* No point in working on empty buffers. */
		return SRD_ERR_ARGS; /* TODO: More specific error? */

	/* TODO: Error handling. */
	py_instance = dec->py_instance;
	Py_XINCREF(py_instance);

	if (!(py_res = PyObject_CallMethod(py_instance, "decode",
					   "{s:i,s:i,s:s#}", 
					   "time", _timehack,
					   "duration", 10,
					   "data", inbuf, inbuflen))) { /* NEWREF */
		if (PyErr_Occurred())
			PyErr_Print(); /* Returns void. */

		return SRD_ERR_PYTHON; /* TODO: More specific error? */
	} 

	Py_XDECREF(py_res);
	return SRD_OK;
}

/* Feed logic samples to decoder session. */
int srd_session_feed(uint8_t *inbuf, uint64_t inbuflen)
{
	GSList *d;
	for (d = decoders; d; d = d->next) {
		/* TODO: Error handling. */
		
		int ret = srd_run_decoder(d->data, inbuf, inbuflen);

		if (ret != SRD_OK) {
			/* This probably shouldn't fail catastrophically. */
			fprintf(stderr, "Decoder runtime error (%d)\n", ret);
			exit(1);
		}
	}
	return SRD_OK;
}

/**
 * TODO
 */
static int srd_unload_decoder(struct srd_decoder *dec)
{
	g_free(dec->id);
	g_free(dec->name);
	g_free(dec->desc);
	g_free(dec->func);

	/* TODO: Free everything in inputformats and outputformats. */

	if (dec->inputformats != NULL)
		g_slist_free(dec->inputformats);
	if (dec->outputformats != NULL)
		g_slist_free(dec->outputformats);

	Py_XDECREF(dec->py_decobj);

	/* TODO: (g_)free dec itself? */

	return SRD_OK;
}

/**
 * TODO
 */
static int srd_unload_all_decoders(void)
{
	GSList *l;
	struct srd_decoder *dec;

	for (l = srd_list_decoders(); l; l = l->next) {
		dec = l->data;
		/* TODO: Error handling. */
		srd_unload_decoder(dec);
	}

	return SRD_OK;
}

/**
 * Shutdown libsigrokdecode.
 *
 * This frees all the memory allocated for protocol decoders and shuts down
 * the Python interpreter.
 *
 * This function should only be called if there was a (successful!) invocation
 * of srd_init() before. Calling this function multiple times in a row, without
 * any successful srd_init() calls inbetween, is not allowed.
 *
 * @return SRD_OK upon success, a (negative) error code otherwise.
 */
int srd_exit(void)
{
	/* Unload/free all decoders, and then the list of decoders itself. */
	/* TODO: Error handling. */
	srd_unload_all_decoders();
	g_slist_free(list_pds);

	/* Py_Finalize() returns void, any finalization errors are ignored. */
	Py_Finalize();

	return SRD_OK;
}
