/*
 * npw-qvd-connection.h
 *
 * 21/8/2010 - Nito@Qindel.ES
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef NPW_QVD_CONNECTION_H
#define NPW_QVD_CONNECTION_H

#ifdef __cplusplus
extern "C" {
#endif
#include <X11/Xlib.h>


#define QVD_REMOTE_INVOCATION_DISABLE_ENV "QVD_NSPLUGIN_DISABLE"
#define QVD_URL_MAX_SIZE 256
#define QVD_REMOTE_INVOCATION_LOCATION "/nsplugin"
#define QVD_REMOTE_INVOCATION_BASEURL "http://localhost:11100"
#define QVD_REMOTE_INVOCATION_URL QVD_REMOTE_INVOCATION_BASEURL QVD_REMOTE_INVOCATION_LOCATION
#define QVD_REMOTE_INVOCATION_URL_ENV "QVD_REMOTE_INVOCATION_URL"
#define QVD_REMOTE_INVOCATION_TIMEOUT_ENV "QVD_REMOTE_INVOCATION_TIMEOUT"
#define QVD_XPROP_TRANSLATION "_QVD_CLIENT_WID"
#define QVD_XPROP_REDIRECT_EVENT "_QVD_REDIRECT_EVENT"
#define QVD_USERAGENT "qvd:slave/1.0"
#define QVD_CONNECT_TIMEOUT_IN_SEC 30

extern int qvd_get_client_socket();
extern int qvd_use_remote_plugin();
extern int qvd_set_remote_plugin();
extern int qvd_unset_remote_plugin();

extern int qvd_invoke_remote_plugin(const char *plugin_name);
extern int qvd_kill_plugin(int socket);
extern void *qvd_translate_wid(void *windowid);
extern void qvd_translate_xevent(XEvent *target, XEvent *source);
extern void qvd_set_xprop_qvd_redirect_event(void *windowid);

#ifdef __cplusplus
}
#endif

#endif
