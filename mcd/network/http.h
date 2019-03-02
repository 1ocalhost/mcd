#pragma once
#include "http_api.h"

BEGIN_NAMESPACE_MCD

using namespace http_api;

class HttpConfig : public IMetaViewer
{
public:
	static const ULONG kInfinite = (ULONG)-1;

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

	void setConnectTimeout(ULONG milliseconds)
	{
		m_connectTimeout = milliseconds;
	}

	ULONG connectTimeout() const
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
	ULONG m_connectTimeout = kInfinite;
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

	Result parse(const std::string& rawHeaders)
	{
		parseRawHeader(rawHeaders);
		uint32_t length;
		_call(parseContentLength(&length));
		m_contentLength = length;
		return {};
	}

	void clear()
	{
		m_headers.clear();
		m_contentLength = (uint32_t)-1;
	}

	uint32_t contentLength() const
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

	Result parseContentLength(uint32_t *result)
	{
		auto& header = (*this)["content-length"];
		if (!_should(header.values.size(), *this)) {
			*result = (uint32_t)-1;
			return {};
		}

		unsigned long lengthNumber = 0;
		const std::string& length = header.values[0];
		bool r = toNumber(length, &lengthNumber);

		_must_or_return(InternalError::invalidInput, r, length);
		_must_or_return(FeatureError::httpBodyOver2GB,
			lengthNumber < GB64(2), length);

		*result = (uint32_t)lengthNumber;
		return {};
	}

	Headers m_headers;
	HeaderValues m_headerNotExists;
	uint32_t m_contentLength = (uint32_t)-1;
};


class HttpResponseBase
{
public:
	typedef uint32_t SizeType;

	void setResponsedSize(uint32_t responsedSize)
	{
		m_sizeTotal = responsedSize;
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

private:
	// WinHTTP donot support 64-bits
	SizeType m_sizeTotal = (SizeType)-1;
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


class HttpRequest : public IMetaViewer
{
public:
	Result init(const HttpConfig& config = {})
	{
		m_headers = config.headers();
		HINTERNET session;
		_call(createSession(&session, config.httpProxy()));
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
	}

	Result open(ConStrRef url, ConStrRef verb)
	{
		_must(m_session, url);
		abortPrevious();

		HttpConnect conn;
		_call(connect(&conn, m_session,
			m_headers, url, verb));

		_must(conn);
		m_connect = conn;
		return receiveResponse();
	}

	Result save(HttpResponseBase* response)
	{
		uint32_t sizeReceived = 0;
		uint32_t sizeTotal = m_contentLength;
		response->setResponsedSize(sizeTotal);

		BinaryData data;
		while (sizeReceived < sizeTotal) {
			_call(readData(m_connect, &data));

			if (data.size == 0)
				break;

			_call(response->write(data));
			sizeReceived += data.size;
		}

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
		m_contentLength = (uint32_t)-1;

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
			<< m_headers
			<< m_statusCode
			<< m_responseHeaders
		;
	}

	int m_statusCode;
	HttpConfig::Headers m_headers;
	HttpHeaders m_responseHeaders;
	uint32_t m_contentLength = (uint32_t)-1;
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
		return HttpRequest::save(&response);
	}
};

END_NAMESPACE_MCD
