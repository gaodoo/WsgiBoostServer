#pragma once
/*
Request handlers for WSGI apps and static files

Copyright (c) 2016 Roman Miroshnychenko <romanvm@yandex.ua>
License: MIT, see License.txt
*/

#include "utils.h"

#include <boost/regex.hpp>
#include <boost/python.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/copy.hpp>

#include <iostream>
#include <unordered_map>
#include <sstream>


namespace WsgiBoost
{
	class BaseRequestHandler
	{
	protected:
		std::ostream& _response;
		const std::string& _server_name;
		const std::string& _http_version;
		const std::string& _method;
		const std::string& _path;
		const std::unordered_multimap<std::string, std::string, ihash, iequal_to>& _in_headers;
		std::vector<std::pair<std::string, std::string>> _out_headers;

	public:
		BaseRequestHandler(
			std::ostream& response,
			const std::string& server_name,
			const std::string& http_version,
			const std::string& method,
			const std::string& path,
			const std::unordered_multimap<std::string, std::string, ihash, iequal_to>& in_headers
		) :
			_response{ response },
			_server_name{ server_name },
			_http_version{ http_version },
			_in_headers{ in_headers },
			_method{ method },
			_path{ path }
		{
			_initialize_headers();
		}

		virtual ~BaseRequestHandler(){}

		BaseRequestHandler(const BaseRequestHandler&) = delete;
		BaseRequestHandler(BaseRequestHandler&&) = delete;

		virtual void handle_request() = 0;

		void send_code(const std::string& code, const std::string message = "")
		{
			_out_headers.emplace_back("Content-Length", std::to_string(message.length()));
			if (message != "")
			{
				_out_headers.emplace_back("Content-Type", "text/plain");
			}
			_send_http_header(code);
			_response << message;
		}

	protected:
		void _initialize_headers()
		{
			_out_headers.emplace_back("Server", _server_name);
			_out_headers.emplace_back("Date", get_current_gmt_time());
			const auto conn_iter = _in_headers.find("Connection");
			if (conn_iter != _in_headers.end())
			{
				_out_headers.emplace_back("Connection", conn_iter->second);
			}
		}

		void _send_http_header(const std::string& code)
		{
			_response << "HTTP/" << _http_version << " " << code << "\r\n";
			for (const auto& header : _out_headers)
			{
				_response << header.first << ": " << header.second << "\r\n";
			}
			_response << "\r\n";
		}
	};


	class StaticRequestHandler : public BaseRequestHandler
	{
	private:
		const std::string& _content_dir;
		const boost::regex& _path_regex;

	public:
		StaticRequestHandler(
				std::ostream& response,
				const std::string& server_name,
				const std::string& http_version,
				const std::string& method,
				const std::string& path,
				const std::string& content_dir,
				const std::unordered_multimap<std::string, std::string, ihash, iequal_to>& in_headers,
				const boost::regex& path_regex
			) : BaseRequestHandler(response, server_name, http_version, method, path, in_headers),
			_content_dir{ content_dir },
			_path_regex{ path_regex }
			{}

		void handle_request()
		{
			const auto content_dir_path = boost::filesystem::path{ _content_dir };
			if (!boost::filesystem::exists(content_dir_path))
			{
				send_code("500 Internal Server Error", "500: Internal server error! Invalid content directory.");
				return;
			}
			if (_method != "GET" && _method != "HEAD")
			{
				std::string code = "405 Method Not Allowed";
				send_code(code, code);
				return;
			}
			_serve_file(content_dir_path);
		}

	private:
		void _serve_file(const boost::filesystem::path& content_dir_path)
		{
			boost::filesystem::path path = content_dir_path;
			path /= boost::regex_replace(_path, _path_regex, "");
			if (boost::filesystem::exists(path))
			{
				path = boost::filesystem::canonical(path);
				// Checking if path is inside content_dir
				if (std::distance(content_dir_path.begin(), content_dir_path.end()) <= std::distance(path.begin(), path.end()) &&
					std::equal(content_dir_path.begin(), content_dir_path.end(), path.begin()))
				{
					if (boost::filesystem::is_directory(path))
					{
						path /= "index.html";
					}
					if (boost::filesystem::exists(path) && boost::filesystem::is_regular_file(path))
					{
						std::ifstream ifs;
						ifs.open(path.string(), std::ifstream::in | std::ios::binary);
						if (ifs)
						{
							time_t last_modified = boost::filesystem::last_write_time(path);
							auto ims_iter = _in_headers.find("If-Modified-Since");
							if (ims_iter != _in_headers.end() && last_modified > header_to_time(ims_iter->second))
							{
								send_code("304 Not Modified");
								return;
							}
							MimeTypes mime_types;
							std::string mime = mime_types[path.extension().string()];			
							_out_headers.emplace_back("Content-Type", mime);
							_out_headers.emplace_back("Last-Modified", time_to_header(last_modified));
							auto ae_iter = _in_headers.find("Accept-Encoding");
							if (mime_types.is_compressable(mime) && ae_iter != _in_headers.end() && ae_iter->second.find("gzip") != std::string::npos)
							{
								boost::iostreams::filtering_istream gzstream;
								gzstream.push(boost::iostreams::gzip_compressor());
								gzstream.push(ifs);
								std::stringstream compressed;
								boost::iostreams::copy(gzstream, compressed);
								_out_headers.emplace_back("Content-Encoding", "gzip");
								if (_method == "HEAD")
								{
									compressed.str("");
								}
								_send_file(compressed);
							}
							else
							{
								if (_method == "GET")
								{
									_send_file(ifs);
								}
								else
								{
									std::stringstream ss;
									ss.str("");
									_send_file(ss);
								}
							}
							ifs.close();
							return;
						}
					}
				}
			}
			std::cerr << "Invalid path: " << path.string() << std::endl;
			send_code("404 Not Found", "Error 404: Requested content not found!");
		}

		void _send_file(std::istream& content_stream)
		{
			content_stream.seekg(0, std::ios::end);
			size_t length = content_stream.tellg();
			content_stream.seekg(0, std::ios::beg);
			_out_headers.emplace_back("Content-Length", std::to_string(length));
			_send_http_header("200 OK");
			//read and send 128 KB at a time
			const size_t buffer_size = 131072;
			SafeCharBuffer buffer{ buffer_size };
			size_t read_length;
			while ((read_length = content_stream.read(buffer.data, buffer_size).gcount()) > 0)
			{
				_response.write(buffer.data, read_length);
				_response.flush();
			}
		}
	};

	
	class WsgiRequestHandler : public BaseRequestHandler
	{
	private:
		const std::string& _host_name;
		unsigned short _port;
		const std::string& _remote_endpoint_address;
		const unsigned short& _remote_endpoint_port;
		const std::unordered_multimap<std::string, std::string, ihash, iequal_to>& _in_headers;
		std::istream& _in_content;
		boost::python::object& _app;	

		boost::python::dict environ_;
		boost::python::object start_response;

	public:
		WsgiRequestHandler(
				std::ostream& response,
				const std::string& server_name,
				const std::string& host_name,
				unsigned short port,
				const std::string& http_version,
				const std::string& method,
				const std::string& path,
				const std::string& remote_endpoint_address,
				const unsigned short& remote_endpoint_port,
				const std::unordered_multimap<std::string, std::string, ihash, iequal_to>& in_headers,
				std::istream& in_content,
				boost::python::object& app
			) : BaseRequestHandler(response, server_name, http_version, method, path, in_headers),
			_host_name{ host_name },
			_port{ port },
			_remote_endpoint_address{ remote_endpoint_address },
			_remote_endpoint_port{ remote_endpoint_port },
			_in_headers{ in_headers },
			_in_content{ in_content },
			_app{ app }
			{
				start_response = boost::python::make_function(&WsgiRequestHandler::_start_response);
			}

		void handle_request()
		{
			if (_app.is_none())
			{
				send_code("500 Internal Server Error", "500: Internal server error! WSGI application is not configured.");
				return;
			}
			std::cout << "Preparing environ..." << std::endl;
			_prepare_environ();
			std::cout << "Getting iterable..." << std::endl;
			PyObject* args = Py_BuildValue("(O,O)", environ_.ptr(), start_response.ptr());
			PyObject* result = PyEval_CallObject(_app.ptr(), args);
			boost::python::object iterable{ boost::python::handle<>(borrowed(result)) };
			std::cout << "Sending iterable..." << std::endl;
			_send_iterable(iterable);
		}

	private:
		void _prepare_environ()
		{
			environ_["SERVER_SOFTWARE"] = _server_name;
			environ_["REQUEST_METHOD"] = _method;
			environ_["SCRIPT_NAME"] = "";
			environ_["PATH_INFO"] = _path;
			environ_["QUERY_STRING"] = get_query_string(_path);
			auto ct_iter = _in_headers.find("Content-Type");
			if (ct_iter != _in_headers.end())
			{
				environ_["CONTENT_TYPE"] = ct_iter->second;
			}
			auto cl_iter = _in_headers.find("Content-Length");
			if (cl_iter != _in_headers.end())
			{
				environ_["CONTENT_LENGTH"] = cl_iter->second;
			}
			environ_["SERVER_NAME"] = _host_name;
			environ_["SERVER_PORT"] = std::to_string(_port);
			environ_["SERVER_PROTOCOL"] = "HTTP/" + _http_version;
			for (auto& header : _in_headers)
			{
				std::string env_header = transform_header(header.first);
				if (!boost::python::extract<bool>(environ_.attr("__contains__")(env_header)))
				{
					environ_[env_header] =  header.second;
				}
				else
				{
					environ_[env_header] = environ_[env_header] + "," + header.second;
				}
			}
			environ_["wsgi.version"] = boost::python::make_tuple<int, int>(1, 0);
			environ_["wsgi.url_scheme"] = _server_name.substr(0, 4);
			std::string in_content;
			_in_content >> in_content;
			boost::python::object input = boost::python::import("cStringIO").attr("StringIO")();
			input.attr("write")(in_content);
			environ_["wsgi.input"] = input;
			boost::python::object errors = boost::python::import("sys").attr("stderr");
			environ_["wsgi.errors"] = errors;
			environ_["wsgi.multithread"] = true;
			environ_["wsgi.multiprocess"] = false;
			environ_["wsgi.run_once"] = false;
		}

		void _start_response(PyObject stat, PyObject resp_hdrs)
		{
			std::string status = boost::python::extract<char*>(stat);
			std::vector<std::pair<std::string, std::string>> response_headers;
			for (int i = 0; i < boost::python::len(resp_hdrs); ++i)
			{
				response_headers[i].first = boost::python::extract<char*>(resp_hdrs[i][0]);
				response_headers[i].second = boost::python::extract<char*>(resp_hdrs[i][1]);
			}
			for (const auto& item : response_headers)
			{
				_out_headers.emplace_back(item);
			}
			_send_http_header(status);
		}

		void _send_iterable(boost::python::object iterable)
		{
			while (true)
			{
				try
				{
					if (PY_MAJOR_VERSION > 2)
					{
						_response << boost::python::extract<char*>(_app.attr("__next__")());
					}
					else
					{
						_response << boost::python::extract<char*>(_app.attr("next")());
					}
				}
				catch (const boost::python::error_already_set& ex)
				{
					PyObject *e, *v, *t;
					PyErr_Fetch(&e, &v, &t);
					PyErr_NormalizeException(&e, &v, &t);
					if (PyErr_GivenExceptionMatches(PyExc_StopIteration, e))
					{
						break;
					}
					else
					{
						PyErr_Restore(e, v, t);
						throw;
					}
				}
			}
		}
	};
}
