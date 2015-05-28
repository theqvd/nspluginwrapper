/*
 * npw-qvd-connection.c
 *
 * We need to use libcurl to fetch a URL for the plugin, because
 * the initialization of the RPC channel is before the plugin is initialized
 * and we don't have the NPP instance for NPP_GetURL (or similar funcs)
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
#include "sysdeps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include "npw-qvd-connection.h"

#define TRACE 1
#define DEBUG 1
#include "debug.h"

/* TODO merge with npw-qvd-connection now it is needed for npw-viewer and npw-wrapper */

static int qvd_use_remote_plugin_flag = -1;
int qvd_use_remote_plugin() {
  if (qvd_use_remote_plugin_flag != -1)
    return qvd_use_remote_plugin_flag;
  
  const char *qvd_disable_remote_invocation = getenv(QVD_REMOTE_INVOCATION_DISABLE_ENV);
  qvd_use_remote_plugin_flag= (qvd_disable_remote_invocation) ? 0 : 1;
  return qvd_use_remote_plugin_flag;
}

int qvd_set_remote_plugin() {
  qvd_use_remote_plugin_flag = 1;
  return qvd_use_remote_plugin_flag;
}
int qvd_unset_remote_plugin() {
  qvd_use_remote_plugin_flag = 0;
  return qvd_use_remote_plugin_flag;
}


/*
 * The client socket will use stdin+stdout
 * we do a dup2, to use these and close stdin+stdout just in case
 */
int qvd_get_client_socket() {
  int s = STDIN_FILENO;
  npw_printf("qvd_get_client_socket: socket = %d\n", s);
  return s;
}

/* TODO set it up in the global g_plugin in npw-wrapper.c */
static CURL *qvd_curl;

/*
 * qvd_invoke_remote_plugin
 * Invokes the remote plugin by sending an http request like this:
 *
 * GET /nsplugin?plugin=flash HTTP/1.1
 * Connection: Upgrade
 * Upgrade: qvd:slave/1.0
 * 
 * This will return a socket, or -1 on error
 */

int qvd_curl_debug_callback(CURL *handle, curl_infotype type,
             unsigned char *data, size_t size,
             void *userp)
{
  const char *text;

  (void)text;
  (void)userp;
  (void)handle; /* prevent compiler warning */

  switch (type) {
  case CURLINFO_TEXT:
    fprintf(stderr, "== Info: %s", data);
  default: /* in case a new one is introduced to shock us */
    return 0;

  case CURLINFO_HEADER_OUT:
    text = "=> Send header";
    break;
  case CURLINFO_DATA_OUT:
    text = "=> Send data";
    break;
  case CURLINFO_HEADER_IN:
    text = "<= Recv header";
    break;
  case CURLINFO_DATA_IN:
    text = "<= Recv data";
    break;
  }

  return 0;
}

long _qvd_connect_get_timeout(void)
{
  static long timeout = -1;
  if (timeout >=0)
    return timeout;
  timeout = QVD_CONNECT_TIMEOUT_IN_SEC;
  const char *timeout_str = getenv(QVD_REMOTE_INVOCATION_TIMEOUT_ENV);
  if (timeout_str) {
	errno = 0;
	long v = strtol(timeout_str, NULL, 10);
	if ((v != LONG_MIN && v != LONG_MAX) || errno != ERANGE) {
	  timeout = v;
	  npw_printf("_get_timeout: Overriding default timeout from %d (environment var %s) to %ld\n", QVD_CONNECT_TIMEOUT_IN_SEC, QVD_REMOTE_INVOCATION_TIMEOUT_ENV, timeout);
	}
  }
  return timeout;
}

int _qvd_switch_protocols(const char *url, const char *location)
{
#define BUFFER_SIZE 65536
  fd_set myset, zero;
  size_t bytes_sent, bytes_received, bytes_received_total;
  int socket, content_length, content_size_parsed, select_res;
  CURLcode res;
  long curlsock;
  char *ptr, *content, buffer_data[BUFFER_SIZE];
  struct timeval timeout;

  npw_printf("_qvd_switch_protocols: initializing curl for url <%s> and location <%s>\n", url, location);
  qvd_curl = curl_easy_init();
  if (!qvd_curl) {
    npw_printf("_qvd_switch_protocols: ERROR initializing curl\n");
    return -1;
  }
  D(bug("_qvd_switch_protocols: Curl initialized %p\n", qvd_curl));

  if (get_debug_level() > 0) {
    D(bug("_qvd_switch_protocols: Curl set to verbose\n"));
    curl_easy_setopt(qvd_curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(qvd_curl, CURLOPT_DEBUGFUNCTION, qvd_curl_debug_callback);
  }

  curl_easy_setopt(qvd_curl, CURLOPT_URL, url);

  curl_easy_setopt(qvd_curl, CURLOPT_TCP_NODELAY, 1L);
  curl_easy_setopt(qvd_curl, CURLOPT_USERAGENT, QVD_USERAGENT);
  curl_easy_setopt(qvd_curl, CURLOPT_PROXY, "");
  curl_easy_setopt(qvd_curl, CURLOPT_CONNECT_ONLY, 1L);

  D(bug("_qvd_switch_protocols: Curl first curl_easy_perform\n"));

  /* TODO set snprintf GET ... */
  if ((res = curl_easy_perform(qvd_curl)) != CURLE_OK ) {
    npw_printf("qvd_switch_protocols: ERROR ocurred in first curl_easy_perform for url %s: %ul <%s>\n", url, res, curl_easy_strerror(res));
    return -1;
  }

  curl_easy_getinfo(qvd_curl, CURLINFO_LASTSOCKET, &socket);


  /* TODO allow to set timeout in env var */
  D(bug("_qvd_switch_protocols: Curl first curl_easy_send\n"));
  FD_ZERO(&myset);
  FD_ZERO(&zero);
  FD_SET(socket, &myset);
  timeout.tv_sec = _qvd_connect_get_timeout();
  timeout.tv_usec = 0;
  select_res = select(socket+1, &zero, &myset, &zero, &timeout);
  if (select_res < 0) {
    npw_printf("qvd_switch_protocols: ERROR ocurred in select waiting for write in curl_easy_send for url %s: %ul <%s>\n", url, select_res, strerror(select_res));
    return -1;
  }
  if (select_res == 0) {
    npw_printf("qvd_switch_protocols: ERROR (timeout) ocurred in select waiting for write in curl_easy_send for url %s. Timed out after %d seconds\n", url, QVD_CONNECT_TIMEOUT_IN_SEC);
    return -1;
  }
  D(bug("_qvd_switch_protocols: Curl first curl_easy_send after select\n"));

  if ((res = curl_easy_send(qvd_curl, location, strlen(location) , &bytes_sent )) != CURLE_OK ) {
    npw_printf("qvd_switch_protocols: ERROR ocurred in first curl_easy_send for url %s: %ul <%s>\n", url, res, curl_easy_strerror(res));
    return -1;
  }

  D(bug("_qvd_switch_protocols: Curl first curl_easy_send res was: %d\n", res));

  FD_ZERO(&myset);
  FD_ZERO(&zero);
  FD_SET(socket, &myset);
  timeout.tv_sec = _qvd_connect_get_timeout();
  timeout.tv_usec = 0;
  /* TODO set timeout  and error ? */
  select_res = select(socket+1, &myset, &zero, &zero, &timeout);
  if (select_res < 0) {
    npw_printf("qvd_switch_protocols: ERROR ocurred in select waiting for read in curl_easy_send for url %s: %ul <%s>\n", url, select_res, strerror(select_res));
    return -1;
  }
  if (select_res == 0) {
    npw_printf("qvd_switch_protocols: ERROR (timeout) ocurred in select waiting for read in curl_easy_send for url %s. Timed out after %d seconds\n", url, QVD_CONNECT_TIMEOUT_IN_SEC);
    return -1;
  }
  if ((res = curl_easy_recv(qvd_curl, buffer_data, BUFFER_SIZE, &bytes_received)) != CURLE_OK ) {
      npw_printf("qvd_switch_protocols: ERROR ocurred in curl_easy_recv: %ul <%s>\n", res, curl_easy_strerror(res));
      return 2;
    }
  buffer_data[bytes_received] = 0;
  D(bug("qvd_switch_protocols: input received was <%s>\n", buffer_data));
  if (strstr(buffer_data, "HTTP/1.1 101")) {
    curl_easy_getinfo(qvd_curl, CURLINFO_LASTSOCKET, &curlsock);  
    int socket = (int) curlsock;
    npw_printf("qvd_switch_protocols: Upgrade of protocol was done\n");
    return socket;
  }

  if (strstr(buffer_data, "HTTP/1.1 2")
      || strstr(buffer_data, "HTTP/1.1 3")
      || strstr(buffer_data, "HTTP/1.1 4")
      || strstr(buffer_data, "HTTP/1.1 5")) {
    bytes_received_total = 0;
    npw_printf("qvd_switch_protocols: ERROR receiving expecting HTTP/1.1 101 but got <%s>\n", buffer_data);
#define CONTENT_LENGTH "\r\nContent-Length: "
    if ((ptr = strstr(buffer_data, CONTENT_LENGTH)) != NULL) {
      ptr += strlen(CONTENT_LENGTH);
#ifdef TRACE
      npw_printf("qvd_switch_protocols: Parsing content length from <%s> and starting in <%s>", buffer_data, ptr);
#endif
      content_length = -1;
      if (sscanf(ptr, "%d", &content_length) != 1) {
	npw_printf("qvd_switch_protocols: ERROR parsing content-length setting to -1: %d", content_length);
	content_length = -1;	  
      }
    }

    while (bytes_received < BUFFER_SIZE) {
      npw_printf("qvd_switch_protocols: Waiting for extra data after found 2xx, 3xx, 4xx or 5xx code <%s>", buffer_data);
      select(socket+1, &myset, &zero, &zero, NULL);
      
      ptr = buffer_data;
      ptr += bytes_received;
      /* TODO implement callback for info */
      if ((res = curl_easy_recv(qvd_curl, ptr, BUFFER_SIZE, &bytes_received_total)) != CURLE_OK ) {
	ptr = strstr(buffer_data, "\r\n\r\n");
	npw_printf("qvd_switch_protocols: Error received in qvd_curl_easy_recv: %d. <%s>", res, ptr);
	return -1;
      }
      bytes_received += bytes_received_total;
#define DOUBLENEWLINE "\r\n\r\n"
      content = strstr(buffer_data, DOUBLENEWLINE);
      content_size_parsed = content != NULL ? strlen(content): -1;
#ifdef TRACE
      npw_printf("The bytes received were: %d, and curle code was: %d, content: <%s>, size of content: %d", (int)bytes_received_total, (int)res, content, content_size_parsed);
#endif
      if (bytes_received == 0 || content_size_parsed >= content_length) {
	content += strlen(DOUBLENEWLINE);
	npw_printf("Error: <%s>", content);
	return -1;
      }
    }
  }
  return -1;
}

const char *qvd_remote_invocation_url() {
  
  static const char *url = NULL;
  if (url) {
    return url;
  }
  if ((url = getenv(QVD_REMOTE_INVOCATION_URL_ENV)) != NULL ) {
    npw_printf("qvd_remote_invocation_url: Overriding default invocation url from %s (environment var %s) to %s\n", QVD_REMOTE_INVOCATION_URL, QVD_REMOTE_INVOCATION_URL_ENV, url);
  } else {
    url = QVD_REMOTE_INVOCATION_URL;
  }

  return url;
}

int qvd_invoke_remote_plugin(const char *plugin_name) {

  char qvd_url[QVD_URL_MAX_SIZE];
  char qvd_location[QVD_URL_MAX_SIZE];

  CURL *curlid = curl_easy_init();
  if (!curlid) {
    npw_printf("qvd_invoke_remote_plugin: ERROR initializing curl\n");
    return -1;
  }

  char *escaped_plugin_name = curl_easy_escape(curlid, plugin_name, 0);


  if (snprintf(qvd_url, QVD_URL_MAX_SIZE, "%s?plugin=%s", qvd_remote_invocation_url(), escaped_plugin_name) >= QVD_URL_MAX_SIZE) {
    npw_printf("qvd_invoke_remote_plugin: ERROR initializing url %s\n", qvd_remote_invocation_url());
    return -1;
  }
  npw_printf("qvd_invoke_remote_plugin: initializing url %s\n", qvd_url);

  if (snprintf(qvd_location, QVD_URL_MAX_SIZE, "GET %s?plugin=%s HTTP/1.1\nConnection: Upgrade\nUpgrade: %s\n\n", QVD_REMOTE_INVOCATION_LOCATION, escaped_plugin_name, QVD_USERAGENT) >= QVD_URL_MAX_SIZE) {
    npw_printf("qvd_invoke_remote_plugin: ERROR initializing location %s\n", qvd_location);
    return -1;
  }
  npw_printf("qvd_invoke_remote_plugin: initializing request %s\n", qvd_location);

  curl_free(escaped_plugin_name);
  curl_easy_cleanup(curlid);

  int socket = _qvd_switch_protocols(qvd_url, qvd_location);

  return socket;
}



/*
 *
 * This should close the socket, this will automaticall kill
 * the plugin.
 *
 */
int qvd_kill_plugin(int s) {
  if (close(s) < 0) {
    npw_printf("qvd_kill_plugin: ERROR close socket %d: %s\n", s, strerror(errno));
  }
  curl_easy_cleanup(qvd_curl);
  return 0;
}

static int _get_window_id_prop_err_handler_result;
static int _get_window_id_prop_err_handler(Display *dpy, XErrorEvent *error)
{
      char buffer1[1024];
      XGetErrorText(dpy, error -> error_code, buffer1, 1024);
      npw_printf("_X Error of failed request:  %s\n_  Major opcode of failed request: %3d \n_ Serial number of failed request:%5ld\n_  Current serial number in output stream:?????\n",
		 buffer1,
		 error -> request_code,
		 error -> serial);
      _get_window_id_prop_err_handler_result = 1;
      return 0;
}


Window get_window_id_prop(Window windowid)
{
  const char *property = QVD_XPROP_TRANSLATION;
  Atom     actual_type;
  int      actual_format;
  unsigned long  nitems;
  unsigned long  bytes;
  long     *data = NULL;
  _get_window_id_prop_err_handler_result = 0;

  Display *display = XOpenDisplay(0);
  if (!display)
    {
      npw_printf("can't open display\n");
      return (0);
    }
  if (windowid == 0) {
      npw_printf("Empty window id\n");
      return (0);
  }

  D(bug("get_window_id_prop, trying to get property : %s for window id 0x%x\n", property, (unsigned)windowid));
  Atom propertyAtom = XInternAtom(display, property, True);
  D(bug("get_window_id_prop, after getting property: %s, %ld\n", property, propertyAtom));
  if (propertyAtom == None)
    {
      npw_printf("The Xwindows property %s does not exists\n", property);
      return 0;
    }
  XSetErrorHandler(_get_window_id_prop_err_handler);
    
  Status result = XGetWindowProperty(
                display,
                windowid,
                propertyAtom, //replace this with your property
                0,
                (~0L),
                False,
                AnyPropertyType,
                &actual_type,
                &actual_format,
                &nitems,
                &bytes,
                (unsigned char**)&data);
  XSetErrorHandler(NULL);

  if (result != Success || _get_window_id_prop_err_handler_result != 0)
    {
      npw_printf("Error getting XGetWindowProperty %s for id 0x%lx\n", property, windowid);
      return (0);
    }
  D(bug ("get_window_id_prop: data actual_type: %ld, format: %d, nitems: %ld, bytes: %ld, data: 0x%lx %ld\n", actual_type, actual_format, nitems, bytes, *data, *data));
  if (nitems < 1 || data == NULL)
    return 0;
  return *data;
}

/*
 * Get the _QVD_WID
 */
void *qvd_translate_wid(void *windowid) {
  Window new_windowid = get_window_id_prop((Window)windowid);

  if (new_windowid == 0)
    {
      new_windowid = (Window)windowid;
      npw_printf("ERROR: qvd_translate_wid returned null, setting the windowid to NULL %p. Please check that you have the correct version of the qvd-nxagent package.\n", (void *)new_windowid);
    }

  D(bug("qvd_translate_wid for translated windowid from %p->%p [%lx]\n", windowid, (void *)new_windowid, new_windowid));
  return (void *)new_windowid;
}


/*
 * qvd_set_xprop_qvd_redirect_event
 * Sets the xprop _QVD_REDIRECT_EVENT to the window
 * specified
 */
void qvd_set_xprop_qvd_redirect_event(void *windowid) {

  Display *display = XOpenDisplay(0);
  if (!display)
    {
      npw_printf("can't open display\n");
      return;
    }

  if (windowid == NULL) {
      npw_printf("Empty window id\n");
      return;
  }

  Atom qvd_client_wid_prop = XInternAtom(display, QVD_XPROP_REDIRECT_EVENT, False);
  if (qvd_client_wid_prop == BadAlloc || 
      qvd_client_wid_prop == BadAtom || 
      qvd_client_wid_prop == None) 
    {
      npw_printf("Error creating %s Atom: %d\n", QVD_XPROP_REDIRECT_EVENT, (unsigned)qvd_client_wid_prop);
      return;
    }

  Window w = (Window) windowid;
  unsigned long value = 0;
  XTextProperty p = { (unsigned char *)&value, XA_INTEGER, 32, 1 };
  XSetTextProperty(display, w, &p, qvd_client_wid_prop);
  XSync(display, False);
  D(bug("Set xprop %s in window %p\n",QVD_XPROP_REDIRECT_EVENT, windowid));

  return;
}

void qvd_translate_xevent(XEvent *target, XEvent *source) {
  D(bug ("qvd_translate_xevent\n"));
  memcpy((void *)target, (void *)source, sizeof(XEvent));
  target->xany.window = (Window)qvd_translate_wid((void *)source->xany.window);
}
