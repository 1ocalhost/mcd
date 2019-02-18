#pragma once
#include "http_api.h"


class HttpConfig : public IMetaViewer
{
public:
	static const ULONG kInfinite = -1;

	typedef std::vector<std::string> Headers;

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
		_should_not(header.find(":") == std::string::npos) << header;
		m_requestHeaders.push_back(header);
	}

	const Headers& requestHeaders(const std::string& header) const
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


class HttpResult : public IMetaViewer
{
public:
	typedef std::map<std::string, std::string> Headers;

	HttpResult() : m_ok(false)
	{
		m_lastError =  GetLastError();
	}

	HttpResult(int status, const std::string& rawHeaders) :
		m_ok(true), m_statusCode(status)
	{
		parseRawHeader(rawHeaders);
	}

	operator bool()
	{
		return m_ok;
	}

	int statusCode()
	{
		return m_statusCode;
	}

	const Headers& headers() const
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
			m_headers.insert(Headers::value_type(
				parser.key(),
				parser.value()
			)); 
		}
	}

	bool m_ok = false;
	int m_statusCode;
	int m_lastError;
	Headers m_headers;
};


class HttpGetRequest
{
public:
	HttpGetRequest(const HttpConfig& config)
	{
		m_session = createSession(
			config.userAgent(),
			config.httpProxy());
		_must(m_session);
	}

	~HttpGetRequest()
	{
		abortPrevious();
		safeRelease(&m_session);
	}

	void abortPrevious()
	{
		m_connect.release();
	}

	HttpResult open(StringViewer url)
	{
		abortPrevious();
		_should(m_session) << url;
		if (!m_session)
			return HttpResult();

		HttpConnect conn = connect(m_session, url);
		_should(conn) << url;
		if (!conn)
			return HttpResult();

		m_connect = conn;
		return receiveResponse();
	}

	bool save(std::string* response)
	{

	}

	bool save(std::ostream* response)
	{

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

		return HttpResult((int)statusCode, rawHeaders);
	}

	HINTERNET m_session = NULL;
	HttpConnect m_connect;
};