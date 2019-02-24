#pragma once
#include "guard.h"


namespace httpapi {

using StringUtil::u8to16;

typedef std::vector<std::string> RequestHeaders;

inline void safeRelease(HINTERNET* handle)
{
	if (*handle) {
		WinHttpCloseHandle(*handle);
		*handle = NULL;
	}
}

inline HINTERNET createSession
(
	ConStrRef proxy = "",
	DWORD flags = NULL
)
{
	const bool toUseProxy = proxy.size();
	DWORD accessType = toUseProxy
		? WINHTTP_ACCESS_TYPE_NAMED_PROXY
		: WINHTTP_ACCESS_TYPE_NO_PROXY;

	return WinHttpOpen(L"", accessType,
		toUseProxy ? u8to16(proxy) : WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, flags);
}

class HttpConnect
{
public:
	HttpConnect() {}
	HttpConnect(HINTERNET con, HINTERNET req) :
		m_connect(con), m_request(req) {}

	operator bool() const { return m_connect && m_request; }
	HINTERNET conn() const { return m_connect; }
	HINTERNET req() const { return m_request; }
	
	void release()
	{
		safeRelease(&m_connect);
		safeRelease(&m_request);
	}

private:
	HINTERNET m_connect = NULL;
	HINTERNET m_request = NULL;
};

inline HINTERNET openRequest
(
	HINTERNET connect,
	ConStrRef verb,
	ConStrRef path,
	bool isSSL = false
)
{
	return WinHttpOpenRequest(connect, u8to16(verb),
		u8to16(path), NULL, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		isSSL ? WINHTTP_FLAG_SECURE : 0);
}

inline bool sendRequest(HINTERNET request)
{
	or_err(request);
	Bool result = WinHttpSendRequest(request,
		WINHTTP_NO_ADDITIONAL_HEADERS, NULL,
		WINHTTP_NO_REQUEST_DATA, NULL,
		0, NULL);

	or_warn(result);

	// wait for headers responsed
	result = WinHttpReceiveResponse(request, NULL);
	or_warn(result);

	return true;
}

inline Bool addRequestHeader(HINTERNET conn, const std::string& header)
{
	or_err(conn);
	Bool result = WinHttpAddRequestHeaders(conn,
		u8to16(header), -1, WINHTTP_ADDREQ_FLAG_ADD);

	or_warn(result, header);
	return true;
}

inline bool addRequestHeaders(
	HINTERNET connect,
	const RequestHeaders& headers
)
{
	for (auto& i : headers)
		or_warn(addRequestHeader(connect, i));

	return true;
}

inline HttpConnect connect
(
	HINTERNET session,
	const RequestHeaders& headers,
	ConStrRef url,
	ConStrRef verb = "GET"
)
{
	using_false(HttpConnect);
	or_err(session, verb, url);

	StringParser::HttpUrl url_(url);
	or_err(url_.valid(), url);

	ResGuard::WinHttp connect = WinHttpConnect(session,
		u8to16(url_.host()), (WORD)url_.port(), NULL);
	or_warn(connect, url);

	ResGuard::WinHttp request = openRequest(
		connect, verb, url_.path(), url_.overSSL());
	or_warn(request, url);

	or_warn(addRequestHeaders(request, headers));
	or_warn(sendRequest(request));
	return HttpConnect(connect.take(), request.take());
}

inline Bool queryHeaders
(
	HINTERNET request,
	LPVOID buffer,
	LPDWORD bufferLength
)
{
	return WinHttpQueryHeaders(request,
		WINHTTP_QUERY_RAW_HEADERS_CRLF,
		WINHTTP_HEADER_NAME_BY_INDEX,
		buffer, bufferLength,
		WINHTTP_NO_HEADER_INDEX);
}

inline bool queryStatusCode(const HttpConnect& conn, int* statusCode)
{
	or_err(conn);
	DWORD statusCode_ = 0;
	DWORD typeSize = sizeof(statusCode_);
	Bool result = WinHttpQueryHeaders(conn.req(),
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX,
		&statusCode_, &typeSize, WINHTTP_NO_HEADER_INDEX);

	or_warn(result);
	*statusCode = statusCode_;
	return true;
}

inline bool queryRawResponseHeaders(
	const HttpConnect& conn, std::string* rawHeaders)
{
	or_err(conn);
	LPVOID buffer = NULL;
	DWORD headerSize = 0;

	// must fail on the first pass
	if (queryHeaders(conn.req(), buffer, &headerSize))
		return false;

	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		return false;

	std::unique_ptr<wchar_t> buffer_(new wchar_t[headerSize]);
	buffer = buffer_.get();

	if (queryHeaders(conn.req(), buffer, &headerSize))
		*rawHeaders = StringUtil::u16to8((PCWSTR)buffer);

	or_warn(rawHeaders->size());
	return true;
}

inline Bool readData(const HttpConnect& conn, BinaryData* data)
{
	return WinHttpReadData(conn.req(),
		data->buffer, data->kBufferSize, &data->size);
}

} // namespace httpapi

using namespace httpapi;
