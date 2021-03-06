/*
Boost Python wrapper for the core WSGI/HTTP servers

Copyright (c) 2016 Roman Miroshnychenko <romanvm@yandex.ua>
License: MIT, see License.txt
*/

#include "server.h"

namespace py = boost::python;
using namespace wsgi_boost;


BOOST_PYTHON_MODULE(wsgi_boost)
{
	PyEval_InitThreads(); // Initialize GIL

	py::register_exception_translator<StopIteration>(&stop_iteration_translator);
	py::register_exception_translator<RuntimeError>(&runtime_error_translator);
	
	py::scope module;
	module.attr("__doc__") = "This module provides WSGI/HTTP server class";
	module.attr("__version__") = WSGI_BOOST_VERSION;
	module.attr("__author__") = "Roman Miroshnychenko";
	module.attr("__email__") = "romanvm@yandex.ua";
	module.attr("__license__") = "MIT";
	py::list all;
	all.append("WsgiBoostHttp");
	module.attr("__all__") = all;
	

	py::class_<HttpServer, boost::noncopyable>("WsgiBoostHttp",

		"WsgiBoostHttp(ip_address='', port=8000, num_threads=1)\n\n"

		"PEP-3333-compliant multi-threaded WSGI server\n\n"

		"The server can serve both Python WSGI applications and static files.\n"
		"For static files gzip compression and 'If-Modified-Since' headers are supported.\n\n"

		":param ip_adderess: server's IP address\n"
		":type ip_address: str\n"
		":param port: server's port\n"
		":type port: int\n"
		":param num_threads: the number of threads for the server to run\n"
		":type num_threads: int\n\n"

		"Usage::\n\n"

		"	import wsgi_boost\n\n"

		"	def hello_app(environ, start_response):\n"
		"		content = 'Hello World!'\n"
		"		status = '200 OK'\n"
		"		response_headers = [('Content-type', 'text/plain'), ('Content-Length', str(len(content)))]\n"
		"		start_response(status, response_headers)\n"
		"		return[content]\n\n"

		"	httpd = wsgi_boost.WsgiBoostHttp(num_threads=4)\n"
		"	httpd.set_app(hello_app)\n"
		"	httpd.start()\n"
		,

		py::init<std::string, unsigned short, unsigned int>((py::arg("ip_address") = "", py::arg("port") = 8000, py::arg("num_threads") = 1)))

		.add_property("is_running", &HttpServer::is_running, "Get server running status")

		.def_readwrite("use_gzip", &HttpServer::use_gzip, "Use gzip compression for static content, default: ``False``")

		.def_readwrite("host_hame", &HttpServer::host_name, "Get or set the host name, default: automatically determined")

		.def_readwrite("header_timeout", &HttpServer::header_timeout,
			"Get or set timeout for receiving HTTP request headers\n\n"

			"This is the max. interval for reciving request headers before closing connection.\n"
			"Defaul: 5s"
			)

		.def_readwrite("content_timeout", &HttpServer::content_timeout,
			"Get or set timeout for receiving POST/PUT content or sending response\n\n"

			"This is the max. interval for receiving POST/PUT content\n"
			"or sending response before closing connection.\n"
			"Defaul: 300s"
			)

		.def_readwrite("url_scheme", &HttpServer::url_scheme,
			"Get os set url scheme -- http or https (Default: ``'http'``)"
			)

		.def("start", &HttpServer::start,
			"Start processing HTTP requests\n\n"
			
			".. note:: This method blocks the current thread until the server is stopped\n"
			"	either by calling :meth:`WsgiBoostHttp.stop` or pressing :kbd:`Ctrl+C`"
			)

		.def("stop", &HttpServer::stop, "Stop processing HTTP requests")

		.def("add_static_route", &HttpServer::add_static_route, py::args("path", "content_dir"),

			"Add a route for serving static files\n\n"

			"Allows to serve static files from ``content_dir``\n\n"

			":param path: a path regex to match URLs for static files\n"
			":type path: str\n"
			":param content_dir: a directory with static files to be served\n"
			":type content_dir: str\n\n"

			".. note:: ``path`` parameter is a regex that must start with ``^/``, for example :regexp:`^/static``\n\n"

			".. warning:: static URL paths have priority over WSGI application paths,\n"
			"    that is, if you set a static route for path :regexp:`^/` *all* requests for ``http://example.com/ \n"
			"    will be directed to that route and a WSGI application will never be reached."
		)

		.def("set_app", &HttpServer::set_app, py::args("app"),
			"Set a WSGI application to be served\n\n"

			":param app: a WSGI application to be served as an executable object\n"
			":raises: RuntimeError on attempt to set a WSGI application while the server is running"
		)
		;


	py::class_<InputWrapper>("InputWrapper", py::no_init)
		.def("read", &InputWrapper::read, (py::arg("size") = -1))
		.def("readline", &InputWrapper::readline, (py::arg("size") = -1))
		.def("readlines", &InputWrapper::readlines, (py::arg("sizehint") = -1))
		.def("__iter__", &InputWrapper::iter, py::return_internal_reference<>())
#if PY_MAJOR_VERSION < 3
		.def("next", &InputWrapper::next)
#else
		.def("__next__", &InputWrapper::next)
#endif
		.def("__len__", &InputWrapper::len)
		;
}
