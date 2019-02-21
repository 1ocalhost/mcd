#pragma once
#include "http_api.h"


class HttpConfig : public IMetaViewer
{
public:
	static const ULONG kInfinite = -1;

	typedef RequestHeaders Headers;

	void setHttpProxy(const std::string& server)
	{
		m_httpProxyServer = server;
	}

	const std::string& httpProxy() const
	{
		return m_httpProxyServer;
	}

	void addRequestHeader(const std::string& header)
	{
		if (header.find(":") == std::string::npos) {
			std::string header_ = header;
			header_.push_back(':');
			m_requestHeaders.push_back(header_);
			return;
		}

		m_requestHeaders.push_back(header);
	}

	const Headers& requestHeaders() const
	{
		return m_requestHeaders;
	}

	void setConnectTimeout(ULONG milliseconds)
	{
		m_connectTimeout = milliseconds;
	}

	ULONG connectTimeout() const
	{
		return m_connectTimeout;
	}

	void setUserAgent(const std::string& agent)
	{
		m_userAgent = agent;
	}

	const std::string& userAgent() const
	{
		return m_userAgent;
	}

	MetaViewerFunc
	{
		return VarDumper()
			<< m_connectTimeout
			<< m_userAgent
			<< m_httpProxyServer
			<< m_requestHeaders
		;
	}

private:
	ULONG m_connectTimeout = kInfinite;
	std::string m_userAgent;
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

	typedef std::map<std::string, HeaderValues> Headers;

	HttpHeaders() {}
	
	HttpHeaders(const std::string& rawHeaders)
	{
		parseRawHeader(rawHeaders);
		m_contentLength = parseContentLength();
	}

	uint32_t contentLength() const
	{
		return m_contentLength;
	}

	// header names are not case sensitive (RFC-2616)
	const HeaderValues& operator [](const std::string& key) const
	{
		auto result = m_headers.find(StringUtil::toLower(key));
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
		auto headerLines = StringUtil::split(rawHeaders, "\r\n", true);
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
		std::string lowerKey = StringUtil::toLower(parser.key());
		auto& mapKey = m_headers[lowerKey];
		mapKey.originalKey = parser.key();
		mapKey.values.push_back(parser.value());
	}

	uint32_t parseContentLength()
	{
		auto& header = (*this)["content-length"];
		_should(header.values.size()) << *this;
		if (!header.values.size())
			return -1;

		const std::string& length = header.values[0];
		try {
			auto len = std::stoul(length);
			_must(len < GB64(2)) << length;
			return len;
		}
		catch (...) {
			_must(false);
			return -1;
		}
	}

	Headers m_headers;
	HeaderValues m_headerNotExists;
	uint32_t m_contentLength = -1;
};

class HttpResult : public IMetaViewer
{
public:
	HttpResult() : m_ok(false)
	{
		m_lastError =  GetLastError();
	}

	HttpResult(int status, const HttpHeaders& headers) :
		m_ok(true), m_statusCode(status), m_headers(headers)
	{
	}

	operator bool()
	{
		return m_ok;
	}

	int statusCode()
	{
		return m_statusCode;
	}

	const HttpHeaders& headers() const
	{
		return m_headers;
	}

	MetaViewerFunc
	{
		return VarDumper()
			<< m_ok
			<< m_statusCode
			<< m_lastError
			<< m_headers
		;
	}

private:
	bool m_ok = false;
	int m_statusCode;
	int m_lastError;
	HttpHeaders m_headers;
};


class HttpResponseBase
{
public:
	void setResponsedSize(uint32_t responsedSize)
	{
		m_sizeTotal = responsedSize;
	}

	virtual bool write(const BinaryData& data)
	{
		m_sizeDone += data.size;
		return true;
	}

private:
	// WinHTTP donot support 64-bits
	uint32_t m_sizeTotal = -1;
	uint32_t m_sizeDone = 0;
};


class HttpResponseString : public HttpResponseBase
{
public:
	HttpResponseString(std::string* str) : m_str(str) {}

	virtual bool write(const BinaryData& data) override
	{
		m_str->append((const char*)data.buffer, data.size);
		return HttpResponseBase::write(data);
	}

private:
	std::string* m_str;
};


class HttpRequest
{
public:
	HttpRequest(const HttpConfig& config) :
		m_headers(config.requestHeaders())
	{
		m_session = createSession(
			config.userAgent(),
			config.httpProxy());
		_must(m_session);
	}

	~HttpRequest()
	{
		abortPrevious();
		safeRelease(&m_session);
	}

	void abortPrevious()
	{
		m_contentLength = -1;
		m_connect.release();
	}

	HttpResult open(ConStrRef url, ConStrRef verb)
	{
		abortPrevious();
		_should(m_session) << url;
		if (!m_session)
			return HttpResult();

		HttpConnect conn = connect(m_session, m_headers, url, verb);
		_should(conn) << url;
		if (!conn)
			return HttpResult();

		m_connect = conn;
		return receiveResponse();
	}

	bool save(HttpResponseBase* response)
	{
		uint32_t sizeReceived = 0;
		uint32_t sizeTotal = m_contentLength;
		response->setResponsedSize(sizeTotal);

		BinaryData data;
		while (sizeReceived < sizeTotal) {
			bool result = readData(m_connect, &data);
			_should(result) << sizeReceived << sizeTotal;
			if (!result)
				return false;

			if (data.size == 0)
				break;

			result = response->write(data);
			_should(result);
			if (!result)
				return false;

			sizeReceived += data.size;
		}

		abortPrevious();
		return true;
	}

private:
	HttpResult receiveResponse()
	{
		// get status code
		int statusCode = 0;
		bool result = queryStatusCode(m_connect, &statusCode);
		_should(result);
		if (!result)
			return HttpResult();

		// get response headers
		std::string rawHeaders;
		result = queryRawResponseHeaders(m_connect, &rawHeaders);
		_should(result);
		if (!result)
			return HttpResult();

		HttpHeaders headers(rawHeaders);
		m_contentLength = headers.contentLength();
		return HttpResult((int)statusCode, headers);
	}

	HttpConfig::Headers m_headers;
	uint32_t m_contentLength = -1;
	HINTERNET m_session = NULL;
	HttpConnect m_connect;
};

class HttpGetRequest : public HttpRequest
{
public:
	HttpGetRequest(const HttpConfig& config) : HttpRequest(config) {}

	HttpResult open(ConStrRef url)
	{
		return HttpRequest::open(url, "GET");
	}

	bool save(std::string* str)
	{
		HttpResponseString response(str);
		return HttpRequest::save(&response);
	}
};