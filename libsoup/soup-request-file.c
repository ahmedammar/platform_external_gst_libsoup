/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-request-file.c: file: URI request object
 *
 * Copyright (C) 2009, 2010 Red Hat, Inc.
 * Copyright (C) 2010 Igalia, S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define LIBSOUP_USE_UNSTABLE_REQUEST_API

#include <glib/gi18n.h>

#include "soup-request-file.h"
#include "soup-directory-input-stream.h"
#include "soup-requester.h"
#include "soup-uri.h"

G_DEFINE_TYPE (SoupRequestFile, soup_request_file, SOUP_TYPE_REQUEST)

struct _SoupRequestFilePrivate {
	GFile *gfile;

	char *mime_type;
	goffset size;
};

static void
soup_request_file_init (SoupRequestFile *file)
{
	file->priv = G_TYPE_INSTANCE_GET_PRIVATE (file, SOUP_TYPE_REQUEST_FILE, SoupRequestFilePrivate);

	file->priv->size = -1;
}

static void
soup_request_file_finalize (GObject *object)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (object);

	if (file->priv->gfile)
		g_object_unref (file->priv->gfile);
	g_free (file->priv->mime_type);

	G_OBJECT_CLASS (soup_request_file_parent_class)->finalize (object);
}

static gboolean
soup_request_file_check_uri (SoupRequest  *request,
			     SoupURI      *uri,
			     GError      **error)
{
	/* "file:/foo" is not valid */
	if (!uri->host)
		return FALSE;

	/* but it must be "file:///..." or "file://localhost/..." */
	if (uri->scheme == SOUP_URI_SCHEME_FILE &&
	    *uri->host &&
	    g_ascii_strcasecmp (uri->host, "localhost") != 0)
		return FALSE;

	return TRUE;
}

static gboolean
soup_request_file_ensure_file (SoupRequestFile  *file,
			       GCancellable     *cancellable,
			       GError          **error)
{
	SoupURI *uri;

	if (file->priv->gfile)
		return TRUE;

	uri = soup_request_get_uri (SOUP_REQUEST (file));
	if (uri->scheme == SOUP_URI_SCHEME_FILE) {
		gchar *decoded_uri = soup_uri_decode (uri->path);

		if (decoded_uri) {
			file->priv->gfile = g_file_new_for_path (decoded_uri);
			g_free (decoded_uri);
		}

		return TRUE;
	}

	g_set_error (error, SOUP_REQUESTER_ERROR, SOUP_REQUESTER_ERROR_UNSUPPORTED_URI_SCHEME,
		     "Unsupported URI scheme '%s'", uri->scheme);
	return FALSE;
}

static GInputStream *
soup_request_file_send (SoupRequest          *request,
			GCancellable         *cancellable,
			GError              **error)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);
	GInputStream *stream;
	GError *my_error = NULL;

	if (!soup_request_file_ensure_file (file, cancellable, error))
		return NULL;

	stream = G_INPUT_STREAM (g_file_read (file->priv->gfile,
					      cancellable, &my_error));
	if (stream == NULL) {
		if (g_error_matches (my_error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY)) {
			GFileEnumerator *enumerator;
			g_clear_error (&my_error);
			enumerator = g_file_enumerate_children (file->priv->gfile,
								"*",
								G_FILE_QUERY_INFO_NONE,
								cancellable,
								error);
			if (enumerator) {
				stream = soup_directory_input_stream_new (enumerator,
									  soup_request_get_uri (request));
				g_object_unref (enumerator);
				file->priv->mime_type = g_strdup ("text/html");
			}
		} else
			g_propagate_error (error, my_error);
	} else {
		GFileInfo *info = g_file_query_info (file->priv->gfile,
						     G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
						     G_FILE_ATTRIBUTE_STANDARD_SIZE,
						     0, cancellable, NULL);
		if (info) {
			const char *content_type;
			file->priv->size = g_file_info_get_size (info);
			content_type = g_file_info_get_content_type (info);

			if (content_type)
				file->priv->mime_type = g_content_type_get_mime_type (content_type);
			g_object_unref (info);
		}
	}

	return stream;
}

static void
soup_request_file_send_async_thread (GSimpleAsyncResult *res,
				     GObject            *object,
				     GCancellable       *cancellable)
{
	GInputStream *stream;
	SoupRequest *request;
	GError *error = NULL;

	request = SOUP_REQUEST (object);

	stream = soup_request_file_send (request, cancellable, &error);

	if (stream == NULL) {
		g_simple_async_result_set_from_error (res, error);
		g_error_free (error);
	} else {
		g_simple_async_result_set_op_res_gpointer (res, stream, g_object_unref);
	}
}

static void
soup_request_file_send_async (SoupRequest          *request,
			      GCancellable         *cancellable,
			      GAsyncReadyCallback callback,
			      gpointer user_data)
{
	GSimpleAsyncResult *res;

	res = g_simple_async_result_new (G_OBJECT (request), callback, user_data, soup_request_file_send_async);

	g_simple_async_result_run_in_thread (res, soup_request_file_send_async_thread, G_PRIORITY_DEFAULT, cancellable);
	g_object_unref (res);
}

static GInputStream *
soup_request_file_send_finish (SoupRequest          *request,
			       GAsyncResult         *result,
			       GError              **error)
{
	GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

	g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == soup_request_file_send_async);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
}

static goffset
soup_request_file_get_content_length (SoupRequest *request)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);

	return file->priv->size;
}

static const char *
soup_request_file_get_content_type (SoupRequest *request)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);

	if (!file->priv->mime_type)
		return "application/octet-stream";

	return file->priv->mime_type;
}

static const char *file_schemes[] = { "file", NULL };

static void
soup_request_file_class_init (SoupRequestFileClass *request_file_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (request_file_class);
	SoupRequestClass *request_class =
		SOUP_REQUEST_CLASS (request_file_class);

	g_type_class_add_private (request_file_class, sizeof (SoupRequestFilePrivate));

	request_class->schemes = file_schemes;

	object_class->finalize = soup_request_file_finalize;

	request_class->check_uri = soup_request_file_check_uri;
	request_class->send = soup_request_file_send;
	request_class->send_async = soup_request_file_send_async;
	request_class->send_finish = soup_request_file_send_finish;
	request_class->get_content_length = soup_request_file_get_content_length;
	request_class->get_content_type = soup_request_file_get_content_type;
}

GFile *
soup_request_file_get_file (SoupRequestFile *file)
{
	return g_object_ref (file->priv->gfile);
}
