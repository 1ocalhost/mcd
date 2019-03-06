#pragma once
#include "http_api.h"

#define _equal_or_return_http_error(http, code, ...) { \
	int response = http.statusCode(); \
	if (!_eval_error(response == code) \
		.setContext(__VA_ARGS__)) \
		return Result("http", response); \
}

BEGIN_NAMESPACE_MCD

using namespace http_api;

class HttpConfig : public IMetaViewer
{
public:
	typedef RequestHeaders Headers;

	void setHttpProxy(const std::string& server)
	{
		m_httpProxyServer = server;
	}

	const std::string& httpProxy() const
	{
		return m_httpProxyServer;
	}

	void addHeader(const std::string& header)
	{
		if (header.find(":") == std::string::npos) {
			std::string header_ = header;
			header_.push_back(':');
			m_requestHeaders.push_back(header_);
			return;
		}

		m_requestHeaders.push_back(header);
	}

	bool hasHeader(ConStrRef name) const
	{
		for (auto& item : m_requestHeaders) {
			const auto& r = split(item, ":");
			if (iEquals(r[0], name)) {
				return true;
			}
		}

		return false;
	}

	const Headers& headers() const
	{
		return m_requestHeaders;
	}

	void setConnectTimeout(int seconds)
	{
		m_connectTimeout = seconds;
	}

	int connectTimeout() const
	{
		return m_connectTimeout;
	}

	MetaViewerFunc
	{
		return VarDumper()
			<< m_connectTimeout
			<< m_httpProxyServer
			<< m_requestHeaders
		;
	}

private:
	int m_connectTimeout = 60;
	std::string m_httpProxyServer;
	Headers m_requestHeaders;
};


class HttpHeaders : public IMetaViewer
{
public:
	struct HeaderValues : public IMetaViewer
	{
		MetaViewerFunc
		{
			return VarDumper()
				<< originalKey
				<< values
			;
		}

		std::string originalKey;
		std::vector<std::string> values;
	};

	class ContentLength
	{
	public:
		typedef int64_t ValueType;

		operator ValueType() const { return m_value; }
		ValueType get() const { return m_value; }
		bool didSet() const { return m_didSet; }

		void operator =(ValueType v)
		{
			assert(v >= 0);
			m_didSet = true;
			m_value = v;
		}

		void reset()
		{
			m_didSet = false;
			m_value = 0;
		}

	private:
		bool m_didSet = false;
		ValueType m_value = 0;
	};

	typedef std::map<std::string, HeaderValues> Headers;

	Result parse(const std::string& rawHeaders)
	{
		parseRawHeader(rawHeaders);
		_call(parseContentLength());
		return {};
	}

	void clear()
	{
		m_headers.clear();
		m_contentLength.reset();
	}

	const ContentLength& contentLength() const
	{
		return m_contentLength;
	}

	// header names are not case sensitive (RFC-2616)
	const HeaderValues& operator [](const std::string& key) const
	{
		auto result = m_headers.find(toLower(key));
		if (result == m_headers.end())
			return m_headerNotExists;

		return result->second;
	}

	bool has(const std::string& key) const
	{
		return (*this)[key].values.size();
	}

	std::string firstValue(const std::string& key) const
	{
		auto& key_ = (*this)[key];
		if (key_.values.empty())
			return std::string();

		return key_.values[0];
	}

	MetaViewerFunc
	{
		return VarDumper()
			<< m_headers
			<< m_contentLength
		;
	}

private:
	void parseRawHeader(const std::string& rawHeaders)
	{
		auto headerLines = split(rawHeaders, "\r\n", true);
		if (headerLines.size())
			headerLines[0].clear();

		StringParser::KeyValue parser(": ");
		for (auto& line : headerLines) {
			if (line.empty())
				continue;

			parser.parse(line);
			addNewHeader(parser);
		}
	}

	void addNewHeader(const StringParser::KeyValue& parser)
	{
		std::string lowerKey = toLower(parser.key());
		auto& mapKey = m_headers[lowerKey];
		mapKey.originalKey = parser.key();
		mapKey.values.push_back(parser.value());
	}

	Result parseContentLength()
	{
		auto& header = (*this)["content-length"];
		if (!_should(header.values.size(), *this)) {
			return {};
		}

		int64_t lengthNumber = 0;
		const std::string& length = header.values[0];
		bool r = toNumber(length, &lengthNumber);

		_must_or_return(InternalError::invalidInput, r, length);
		_must_or_return(InternalError::invalidInput,
			lengthNumber >= 0);
		_must_or_return(FeatureError::httpBodyOver2GB,
			lengthNumber < GB64(2), length);

		m_contentLength = lengthNumber;
		return {};
	}

	Headers m_headers;
	HeaderValues m_headerNotExists;
	ContentLength m_contentLength;
};


class HttpResponseBase
{
public:
	typedef HttpHeaders::ContentLength ContentLength;
	typedef ContentLength::ValueType SizeType;

	void setResponsedSize(ContentLength cl)
	{
		m_cl = cl;
	}

	virtual Result write(const BinaryData& data)
	{
		m_sizeDone += data.size;
		return {};
	}

	SizeType sizeDone() const
	{
		return m_sizeDone;
	}

	SizeType sizeTotal() const
	{
		return m_cl.get();
	}

	bool didSetSizeTotal() const
	{
		return m_cl.didSet();
	}

	void clear()
	{
		m_cl.reset();
		m_sizeDone = 0;
	}

private:
	ContentLength m_cl;
	SizeType m_sizeDone = 0;
};

class HttpResponseString : public HttpResponseBase
{
public:
	HttpResponseString(std::string* str, SizeType maxLenth = KB(10)) :
		m_str(str), m_maxLenth(maxLenth) {}

	virtual Result write(const BinaryData& data) override
	{
		_must_or_return(InternalError::exceedLimit,
			sizeDone() <= m_maxLenth, m_maxLenth);

		m_str->append((const char*)data.buffer, data.size);
		return HttpResponseBase::write(data);
	}

private:
	std::string* m_str;
	SizeType m_maxLenth;
};

class HttpDownloadFileWriter : public HttpResponseBase
{
public:
	Result init(ConStrRef path, int64_t pos, bool reuse)
	{
		if (m_file.is_open()) {
			if (!reuse) {
				m_file.close();
				m_file.open(path, std::ios::binary);
			}
		}
		else {
			m_file.open(path, std::ios::binary);
		}

		m_file.seekp(pos);
		_must_or_return(InternalError::ioError, m_file.good());
		return {};
	}

	virtual Result write(const BinaryData& data) override
	{
		_must(m_file.good());
		m_file.write((char*)data.buffer, data.size);
		_must_or_return(InternalError::ioError, m_file.good());
		return HttpResponseBase::write(data);
	}

private:
	std::ofstream m_file;
};

class ParallelFileWriter
{
public:
	Result init(ConStrRef path)
	{
		m_file.open(path, std::ios::binary);
		_must_or_return(InternalError::ioError, m_file.good());
		return {};
	}

	void abort()
	{
		m_aborted = true;
	}

	Result write(const BinaryData& data, int64_t pos)
	{
		if (m_aborted)
			return InternalError::forceAbort();

		Guard::Mutex lock(&m_mutex);
		m_file.seekp(pos);
		m_file.write((char*)data.buffer, data.size);
		_must(m_file.good());
		_must_or_return(InternalError::ioError, m_file.good());
		return {};
	}

private:
	bool m_aborted = false;
	std::mutex m_mutex;
	std::ofstream m_file;
};

class HttpProxyWriter : public HttpResponseBase
{
public:
	HttpProxyWriter(ParallelFileWriter* writer) :
		m_writer(writer) {}

	void init(int64_t pos)
	{
		m_pos = pos;
	}

	virtual Result write(const BinaryData& data) override
	{
		_call(m_writer->write(data, m_pos));
		m_pos += data.size;
		return HttpResponseBase::write(data);
	}

private:
	int64_t m_pos = 0;
	ParallelFileWriter* m_writer = nullptr;
};

class HttpRequest : public IMetaViewer
{
public:
	Result init(const HttpConfig& config = {})
	{
		m_headers = config.headers();
		HINTERNET session;
		_call(createSession(
			&session,
			config.httpProxy(),
			config.connectTimeout()
		));

		_must(session);
		m_session = session;
		return {};
	}

	~HttpRequest()
	{
		abortPrevious();
		safeRelease(&m_session);
	}

	void abort()
	{
		abortPrevious();
		m_userAborted = true;
	}

	Result open(ConStrRef url, ConStrRef verb)
	{
		_must(m_session, url);
		abortPrevious();
		m_userAborted = false;

		HttpConnect conn;
		_call(connect(&conn, m_session,
			m_headers, url, verb));

		_must(conn);
		m_connect = conn;
		return receiveResponse();
	}

	Result saveResponse(HttpResponseBase* response)
	{
		int64_t sizeReceived = 0;
		int64_t sizeTotal = m_contentLength;
		response->setResponsedSize(m_contentLength);

		BinaryData data;
		while (sizeReceived < sizeTotal) {
			_call(readData(m_connect, &data));

			if (data.size == 0)
				break;

			_call(response->write(data));
			sizeReceived += data.size;
		}

		if (m_userAborted)
			return InternalError::userAbort();

		return {};
	}

	int statusCode() const
	{
		return m_statusCode;
	}

	const HttpHeaders& headers() const
	{
		return m_responseHeaders;
	}

private:
	void abortPrevious()
	{
		m_statusCode = 0;
		m_responseHeaders.clear();
		m_contentLength.reset();

		// ERROR_INTERNET_OPERATION_CANCELLED (12017)
		//   The operation was canceled, usually because the handle on
		//   which the request was operating was closed before the
		//   operation completed.
		m_connect.release();
	}

	Result receiveResponse()
	{
		// get status code
		int statusCode = 0;
		_call(queryStatusCode(m_connect, &statusCode));

		// get response headers
		std::string rawHeaders;
		_call(queryRawResponseHeaders(m_connect, &rawHeaders));

		HttpHeaders headers;
		_call(headers.parse(rawHeaders));

		m_contentLength = headers.contentLength();
		m_statusCode = (int)statusCode;
		m_responseHeaders = headers;
		return {};
	}

	MetaViewerFunc
	{
		return VarDumper()
			<< m_userAborted
			<< m_headers
			<< m_statusCode
			<< m_responseHeaders
		;
	}

	bool m_userAborted = false;
	int m_statusCode;
	HttpConfig::Headers m_headers;
	HttpHeaders m_responseHeaders;
	HttpHeaders::ContentLength m_contentLength;
	HINTERNET m_session = NULL;
	HttpConnect m_connect;
};

class HttpGetRequest : public HttpRequest
{
public:
	Result open(ConStrRef url)
	{
		return HttpRequest::open(url, "GET");
	}

	Result save(std::string* str)
	{
		HttpResponseString response(str);
		return saveResponse(&response);
	}
};

END_NAMESPACE_MCD
