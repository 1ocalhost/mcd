#pragma once
#include "../infra/guard.h"

#define _must_or_return_winhttp_error(result, ...) { \
	DWORD err = GetLastError(); \
	if (!_eval_error(result).setContext(__VA_ARGS__)) \
		return Result(resultSpace(), err); \
}

BEGIN_NAMESPACE_MCD

namespace http_api {

inline const char* resultSpace()
{
	return "winhttp";
}

typedef std::vector<std::string> RequestHeaders;

inline void safeRelease(HINTERNET* handle)
{
	if (*handle) {
		WinHttpCloseHandle(*handle);
		*handle = NULL;
	}
}

inline HINTERNET createSessionWrapper
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

inline Result createSession(HINTERNET *session,
	ConStrRef proxy, int timeoutSeconds)
{
	HINTERNET session_ = createSessionWrapper(proxy);
	_must_or_return_winhttp_error(session_, proxy);

	Bool result = WinHttpSetTimeouts(session_,
		5 * 1000, // DNS
		timeoutSeconds * 1000, // connect
		timeoutSeconds * 1000, // send
		timeoutSeconds * 1000 // receive
	);
	_must_or_return_winhttp_error(result);

	*session = session_;
	return {};
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

inline Result sendRequest(HINTERNET request)
{
	_must(request);
	Bool result = WinHttpSendRequest(request,
		WINHTTP_NO_ADDITIONAL_HEADERS, NULL,
		WINHTTP_NO_REQUEST_DATA, NULL,
		0, NULL);
	_must_or_return_winhttp_error(result);

	// wait for headers responsed
	result = WinHttpReceiveResponse(request, NULL);
	_must_or_return_winhttp_error(result);
	return {};
}

inline Result addRequestHeader(HINTERNET conn, const std::string& header)
{
	_must(conn);
	Bool result = WinHttpAddRequestHeaders(conn,
		u8to16(header), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

	_must_or_return_winhttp_error(result, header);
	return {};
}

inline Result addRequestHeaders(
	HINTERNET connect,
	const RequestHeaders& headers
)
{
	for (auto& i : headers) {
		_call(addRequestHeader(connect, i));
	}

	return {};
}

inline Result connect
(
	HttpConnect *result,
	HINTERNET session,
	const RequestHeaders& headers,
	ConStrRef url,
	ConStrRef verb = "GET"
)
{
	_must(session, verb, url);

	StringParser::HttpUrl url_(url);
	_must_or_return(InternalError::invalidInput, url_.valid(), url);

	Guard::WinHttp connect = WinHttpConnect(session,
		u8to16(url_.host()), (WORD)url_.port(), NULL);
	_must_or_return_winhttp_error(connect.get(), url);

	Guard::WinHttp request = openRequest(
		connect.get(), verb, url_.path(), url_.overSSL());
	_must_or_return_winhttp_error(request.get(), url);

	_call(addRequestHeaders(request.get(), headers));
	_call(sendRequest(request.get()));

	*result = HttpConnect(connect.release(), request.release());
	return {};
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

inline Result queryStatusCode(const HttpConnect& conn, int* statusCode)
{
	_must(conn);
	DWORD statusCode_ = 0;
	DWORD typeSize = sizeof(statusCode_);
	Bool result = WinHttpQueryHeaders(conn.req(),
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX,
		&statusCode_, &typeSize, WINHTTP_NO_HEADER_INDEX);

	_must_or_return_winhttp_error(result);
	*statusCode = statusCode_;
	return {};
}


inline Result queryRawResponseHeaders(
	const HttpConnect& conn, std::string* rawHeaders)
{
	_must(conn);
	LPVOID buffer = NULL;
	DWORD headerSize = 0;

	// must fail on the first pass
	bool r = queryHeaders(conn.req(), buffer, &headerSize);
	_must_not(r);
	_must(GetLastError() == ERROR_INSUFFICIENT_BUFFER);

	std::unique_ptr<wchar_t> buffer_(new wchar_t[headerSize]);
	buffer = buffer_.get();

	r = queryHeaders(conn.req(), buffer, &headerSize);
	_must_or_return_winhttp_error(r);
	*rawHeaders = u16to8((PCWSTR)buffer);
	_must(rawHeaders->size());

	return {};
}

inline Result readData(const HttpConnect& conn, BinaryData* data)
{
	Bool r = WinHttpReadData(conn.req(),
		data->buffer, data->kBufferSize, &data->size);
	_must_or_return_winhttp_error(r);
	return {};
}

} // namespace http_api

END_NAMESPACE_MCD
