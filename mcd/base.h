#pragma once
#include <Windows.h>
#include <shlwapi.h>

#include <string>
#include <sstream>
#include <memory>
#include <vector>
#include <codecvt>
#include <regex>
#include <map>
#include <assert.h>

#define KB(value) (value * 1024)
#define MB(value) (KB(value) * 1024)
#define GB(value) (MB(value) * 1024)


namespace base
{

inline bool debugMode()
{
#ifdef _DEBUG
	return true;
#else
	return false;
#endif
}


class StringViewer8
{
public:
	StringViewer8(const char* str) : m_str(str) {}
	StringViewer8(const std::string& str) : m_str(str.c_str()) {}
	bool notEmpty() const { return m_str && strlen(m_str); }
	bool empty() const { return !notEmpty(); }
	const char* get() { return m_str; }
	operator const char* () { return m_str; }

private:
	const char* m_str;
};

class StringViewer16
{
public:
	StringViewer16(const wchar_t* str) : m_str(str) {}
	StringViewer16(const std::wstring& str) : m_str(str.c_str()) {}
	bool notEmpty() const { return m_str && wcslen(m_str); }
	bool empty() const { return !notEmpty(); }
	const wchar_t* get() { return m_str; }
	operator const wchar_t* () { return m_str; }

private:
	const wchar_t* m_str;
};

typedef StringViewer8 StringViewer;


namespace StringCvt
{
inline std::wstring u8to16(StringViewer8 u8)
{
	if (u8.empty())
		return std::wstring();

	return std::wstring_convert<
		std::codecvt_utf8_utf16<wchar_t>, wchar_t>()
		.from_bytes(u8.get());
}

inline std::string u16to8(StringViewer16 u16)
{
	if (u16.empty())
		return std::string();

	return std::wstring_convert<
		std::codecvt_utf8_utf16<wchar_t>, wchar_t>()
		.to_bytes(u16.get());
}
} // namespace StringCvt

class String16
{
public:
	String16(StringViewer8 u8) : m_str(StringCvt::u8to16(u8)) {}
	operator const wchar_t* () { return m_str.c_str(); }

private:
	std::wstring m_str;
};

class String8
{
public:
	String8(StringViewer16 u16) : m_str(StringCvt::u16to8(u16)) {}
	operator const char* () { return m_str.c_str(); }

private:
	std::string m_str;
};


namespace StringUtil
{
inline std::vector<std::string> split(
	const std::string& str,
	const std::string& delimiter,
	bool ignoreEmpty = false
)
{
	class Result : public std::vector<std::string>
	{
	public:
		Result(bool ignoreEmpty) :
			m_ignoreEmpty(ignoreEmpty) {}

		void add(const std::string& token)
		{
			if (m_ignoreEmpty && token.empty())
				return;

			push_back(token);
		}

	private:
		bool m_ignoreEmpty;
	};

	Result result(ignoreEmpty);
	std::string s = str;
	size_t pos = 0;
	std::string token;
	while ((pos = s.find(delimiter)) != std::string::npos) {
		token = s.substr(0, pos);
		result.add(token);
		s.erase(0, pos + delimiter.length());
	}
	result.add(s);

	return result;
}

inline void escapeChar(std::string* str)
{
	auto hasSpecialChar = [=]() -> bool {
		for (auto& i : *str) {
			switch (i) {
			case '\r':
			case '\n':
			case '\t':
				return true;
			}
		}

		return false;
	};

	if (!hasSpecialChar())
		return;

	std::string& s = *str;
	for (auto i : { "\r\\r", "\n\\n", "\t\\t" }) {
		char ch = i[0];
		const char* literal = &i[1];
		if (s.find(ch) != std::string::npos)
			s = std::regex_replace(s, std::regex(literal), literal);
	}
}
} // namespace StringUtil


namespace StringParser
{

class URI
{
public:
	URI() {}

	URI(StringViewer uri)
	{
		parse(uri);
	}

	bool parse(StringViewer uri)
	{
		clear();
		std::cmatch result;
		std::regex rule(
			R"((([\da-zA-Z-]+)://)?([\da-zA-Z\.-]+)(:(\d{1,5}))?(/(.*))?)");
		
		m_valid = std::regex_match(uri.get(), result, rule);
		if (m_valid) {
			assert(result.size() == 8);
			m_scheme = result[2].str();
			m_host = result[3].str();
			m_port = result[5].str();
			m_path = result[6].str();
		}

		return m_valid;
	}

	bool valid() const { return m_valid; }
	const std::string& scheme() const { return m_scheme; }
	const std::string& host() const { return m_host; }
	const std::string& port() const { return m_port; }
	const std::string& path() const { return m_path; }

private:
	void clear()
	{
		m_valid = false;
		m_scheme.clear();
		m_host.clear();
		m_port.clear();
		m_path.clear();
	}

	bool m_valid = false;
	std::string m_scheme;
	std::string m_host;
	std::string m_port;
	std::string m_path;
};

class HttpUrl
{
public:
	HttpUrl() {}

	HttpUrl(StringViewer url)
	{
		parse(url);
	}

	bool parse(StringViewer url)
	{
		clear();
		URI uri(url);
		if (!uri.valid())
			return false;

		if (uri.host().empty())
			return false;

		auto& scheme = uri.scheme();
		bool validScheme = (scheme.empty()
			|| scheme == "http" || scheme == "https");
		if (!validScheme)
			return false;

		m_valid = true;
		setDefault(uri);

		return m_valid;
	}

	bool valid() const { return m_valid; }
	bool overSSL() const { return m_overSSL; }
	const std::string& scheme() const { return m_scheme; }
	const std::string& host() const { return m_host; }
	int port() const { return m_port; }
	const std::string& path() const { return m_path; }

private:
	void setDefault(const URI& uri)
	{
		m_host = uri.host();
		m_overSSL = (uri.scheme() == "https");
		m_scheme = m_overSSL ? "https" : "http";
		m_port = m_overSSL ? 443 : 80;

		if (uri.port().size())
			m_port = (short)std::stoi(uri.port());

		m_path = uri.path().size() ? uri.path() : "/";
	}

	void clear()
	{
		m_valid = false;
		m_overSSL = false;
		m_scheme.clear();
		m_host.clear();
		m_port = 0;
		m_path.clear();
	}

	bool m_valid = false;
	bool m_overSSL = false;
	std::string m_scheme;
	std::string m_host;
	int m_port;
	std::string m_path;
};

class KeyValue
{
public:
	KeyValue(const std::string& delimiter)
		: m_delimiter(delimiter) {}

	void parse(const std::string& str)
	{
		clear();
		const std::string& s = str;
		auto pos = s.find(m_delimiter);
		if (pos == std::string::npos) {
			m_key = s;
			return;
		}

		m_key = s.substr(0, pos);
		m_value = s.substr(pos + m_delimiter.length());
	}

	const std::string& key() const { return m_key; }
	const std::string& value() const { return m_value; }

private:
	void clear()
	{
		m_key.clear();
		m_value.clear();
	}

	std::string m_delimiter;
	std::string m_key;
	std::string m_value;
};

} // namespace StringParser

class Bool
{
public:
	Bool(bool b) : m_b(b) {}
	Bool(BOOL b) : m_b(b != FALSE) {}
	operator bool() const { return m_b; }

private:
	bool m_b;
};

} // namespace base

using namespace base;
