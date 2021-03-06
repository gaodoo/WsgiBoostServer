#pragma once
/*
Core HttpServer class

Copyright (c) 2016 Roman Miroshnychenko <romanvm@yandex.ua>
License: MIT, see License.txt
*/

#include "request_handlers.h"

#include <thread>
#include <string>
#include <vector>
#include <atomic>


namespace wsgi_boost
{
	class HttpServer
	{
	private:
		boost::asio::io_service m_io_service;
		boost::asio::ip::tcp::acceptor m_acceptor;
		std::vector<std::thread> m_threads;
		unsigned int m_num_threads;
		std::string m_ip_address;
		unsigned short m_port;
		boost::asio::signal_set m_signals;
		std::vector<std::pair<boost::regex, std::string>> m_static_routes;
		boost::python::object m_app;
		std::atomic_bool m_is_running;

		void accept();
		void process_request(socket_ptr socket);
		void check_static_route(Request& request);
		void handle_request(Request& request, Response& response);

	public:
		unsigned int header_timeout = 5;
		unsigned int content_timeout = 300;
		bool reuse_address = true;
		std::string url_scheme = "http";
		std::string host_name;
		bool use_gzip = true;

		HttpServer(const HttpServer&) = delete;
		HttpServer& operator=(const HttpServer&) = delete;

		HttpServer(std::string ip_address = "", unsigned short port = 8000, unsigned int num_threads = 1);

		// Add a path to static content
		void add_static_route(std::string path, std::string content_dir);

		// Set WSGI application
		void set_app(boost::python::object app);

		// Start handling HTTP requests
		void start();

		// Stop handling http requests
		void stop();

		// Check if the server is running
		bool is_running() const;
	};
}
