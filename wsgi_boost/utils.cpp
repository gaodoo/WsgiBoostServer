#include "utils.h"

using namespace std;

namespace wsgi_boost
{
#pragma region StringQueue

	void StringQueue::push(string item)
	{
		m_mutex.lock();
		m_queue.push(item);
		m_mutex.unlock();
	}

	string StringQueue::pop()
	{
		std::string tmp;
		m_mutex.lock();
		tmp = m_queue.front();
		m_queue.pop();
		m_mutex.unlock();
		return tmp;
	}

	bool String::Queue::is_empty()
	{
		bool tmp;
		m_mutex.lock();
		tmp = m_queue.empty();
		m_mutex.unlock();
		return tmp;
	}

#pragma endregion

	string time_to_header(time_t posix_time)
	{
		stringstream ss;
		ss.imbue(std::locale("C"));
		ss << put_time(std::gmtime(&posix_time), "%a, %d %b %Y %H:%M:%S GMT");
		return ss.str();
	}


	time_t header_to_time(const string& time_string)
	{
		tm t;
		stringstream ss{ time_string };
		ss.imbue(std::locale("C"));
		ss >> get_time(&t, "%a, %d %b %Y %H:%M:%S GMT");
		return mktime(&t);
	}


	pair<string, string> split_path(const string& path)
	{
		size_t pos = path.find("?");
		if (pos != string::npos)
		{
			return pair<string, string>{path.substr(0, pos), path.substr(pos + 1)};
		}
		return pair<string, string>(path, "");
	}


	string transform_header(string header)
	{
		for (auto& ch : header)
		{
			if (ch == '-')
				ch = '_';
		}
		boost::to_upper(header);
		header = "HTTP_" + header;
		return header;
	}


	string get_current_local_time()
	{
		time_t t = std::time(nullptr);
		stringstream ss;
		ss.imbue(std::locale("C"));
		ss << put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
		return ss.str();
	}
}
