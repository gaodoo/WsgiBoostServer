#include "server.h"

#include <boost/asio/spawn.hpp>

#include <memory>
#include <csignal>

using namespace std;
namespace asio = boost::asio;
namespace sys = boost::system;
namespace py = boost::python;

namespace wsgi_boost
{
	HttpServer::HttpServer(std::string ip_address, unsigned short port, unsigned int num_threads) :
		m_ip_address{ ip_address }, m_port{ port }, m_num_threads{ num_threads }, m_acceptor(m_io_service), m_signals{ m_io_service }
	{
		m_signals.add(SIGINT);
		m_signals.add(SIGTERM);
#if defined(SIGQUIT)
		m_signals.add(SIGQUIT);
#endif // defined(SIGQUIT)
	}


	void HttpServer::accept()
	{
		socket_ptr socket = make_shared<asio::ip::tcp::socket>(asio::ip::tcp::socket(m_io_service));
		m_acceptor.async_accept(*socket, [this, socket](const boost::system::error_code& ec)
		{
			accept();
			if (!ec)
			{
				socket->set_option(asio::ip::tcp::no_delay(true));
				process_request(socket);
			}
		});
	}


	void HttpServer::process_request(socket_ptr socket)
	{		
		Connection connection{ socket, m_io_service, header_timeout, content_timeout };
		Request request{ connection };
		Response response{ connection };
		sys::error_code ec = request.parse_header();
		if (!ec)
		{
			check_static_route(request);
			response.http_version == request.http_version;
			handle_request(request, response);
		}
		else if (ec == sys::errc::bad_message)
		{
			response.send_mesage("400 Bad Request");
		}
		else if (ec == sys::errc::invalid_argument)
		{
			response.send_mesage("411 Length Required");
		}
	}

	void HttpServer::check_static_route(Request& request)
	{
		for (const auto& route : m_static_routes)
		{
			if (boost::regex_search(request.path, route.first))
			{
				request.path_regex = route.first;
				request.content_dir = route.second;
				break;
			}
		}
	}

	void HttpServer::handle_request(Request& request, Response& response)
	{
		string message = "Error 500: Internal server error!\n\n";
		if (request.content_dir == string())
		{
			request.url_scheme = url_scheme;
			request.host_name = m_host_name;
			request.local_endpoint_port = m_port;
			GilAcquire acquire_gil;
			WsgiRequestHandler handler{ request, response, m_app };
			try
			{
				handler.handle();
			}
			catch (const py::error_already_set&)
			{
				if (wsgi_debug)
				{
					stop();
					return;
				}
				else
				{
					PyErr_Print();
				}
				response.send_mesage("500 Internal Server Error", message);
			}
			catch (const exception& ex)
			{
				cerr << ex.what() << endl;
				response.send_mesage("500 Internal Server Error", message);
			}
		}
		else
		{
			StaticRequestHandler handler{ request, response };
			try
			{
				handler.handle();
			}
			catch (const exception& ex)
			{
				cerr << ex.what();
				response.send_mesage("500 Internal Server Error", message);
			}
		}
		if (request.persistent())
			process_request(request.connection().socket());
	}


	void HttpServer::add_static_route(string path, string content_dir)
	{
		m_static_routes.emplace_back(boost::regex(path, boost::regex_constants::icase), content_dir);
	}


	void HttpServer::set_app(py::object app)
	{
		m_app = app;
	}


	void HttpServer::start()
	{
		cout << "Starting WsgiBoostHttp server..." << endl;
		GilRelease release_gil;
		if (m_io_service.stopped())
			m_io_service.reset();
		asio::ip::tcp::endpoint endpoint;
		if (m_ip_address != "")
		{
			asio::ip::tcp::resolver resolver(m_io_service);
			try
			{
				endpoint = *resolver.resolve({ m_ip_address, to_string(m_port) });
			}
			catch (const exception&)
			{
				ostringstream oss;
				oss << "Unable to reslove IP address " << m_ip_address << " and port " << m_port << "!";
				throw RuntimeError(oss.str());
			}
		}
		else
		{
			endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), m_port);
		}
		m_acceptor.open(endpoint.protocol());
		m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(reuse_address));
		m_acceptor.bind(endpoint);
		m_acceptor.listen();
		m_host_name = asio::ip::host_name();
		accept();
		m_threads.clear();
		for (unsigned int i = 1; i < m_num_threads; ++i)
		{
			m_threads.emplace_back([this]()
			{
				m_io_service.run();
			});
		}
		m_signals.async_wait([this](sys::error_code, int) { stop(); });
		m_io_service.run();
		for (auto& t : m_threads)
		{
			t.join();
		}
	}


	void HttpServer::stop()
	{
		if (is_running())
		{
			m_acceptor.close();
			m_io_service.stop();
			m_signals.cancel();
			cout << "WsgiBoostHttp server stopped." << endl;
		}
	}
}
