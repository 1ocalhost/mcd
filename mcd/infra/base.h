#pragma once
#define NOMINMAX

#include <Windows.h>
#include <shlwapi.h>

#include <string>
#include <sstream>
#include <memory>
#include <vector>
#include <codecvt>
#include <regex>
#include <map>
#include <functional>

#define KB(value) ((value) * 1024)
#define MB(value) (KB(value) * 1024)
#define GB(value) (MB(value) * 1024)
#define KB64(value) ((value ## ull) * 1024)
#define MB64(value) (KB(value ## ull) * 1024)
#define GB64(value) (MB(value ## ull) * 1024)

#define unless(x) if (!(x))
#define UNUSED UNREFERENCED_PARAMETER

#define BEGIN_NAMESPACE_MCD namespace mcd {
#define END_NAMESPACE_MCD }

BEGIN_NAMESPACE_MCD

inline bool debugMode()
{
#ifdef _DEBUG
	return true;
#else
	return false;
#endif
}

class InterfaceClass
{
public:
	virtual ~InterfaceClass() {}
};

template<class T, size_t N>
constexpr size_t _sizeof(T(&)[N]) { return N; }

template <class T>
inline bool inRange(T v, T begin, T end)
{
	return v >= begin && v < end;
}

typedef const std::string& ConStrRef;
typedef const std::wstring& ConWStrRef;

namespace StringUtil
{

template <class Ch>
class basic_strx : public std::basic_string<Ch>
{
public:
	typedef std::basic_string<Ch> SuperType;

	basic_strx() {}
	basic_strx(const SuperType& str) : SuperType(str) {}
	basic_strx(const SuperType&& str) : SuperType(str) {}
	operator const Ch* () { return this->c_str(); }
};

typedef basic_strx<char> stringx;
typedef basic_strx<wchar_t> wstringx;

typedef std::wstring_convert<
	std::codecvt_utf8_utf16<wchar_t>, wchar_t> utf8_16_cvt;

inline wstringx u8to16(ConStrRef u8)
{
	return u8.empty() ? wstringx()
		: utf8_16_cvt().from_bytes(u8.c_str());
}

inline stringx u16to8(ConWStrRef u16)
{
	return u16.empty() ? stringx()
		: utf8_16_cvt().to_bytes(u16.c_str());
}

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
			unless (m_ignoreEmpty && token.empty())
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

inline void escapeBlankChar(std::string* str)
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

template <class T>
T toLower(T x) { return (T)::tolower((T)x); }

inline std::string& toLower(std::string* str)
{
	std::transform(str->begin(), str->end(), str->begin(), toLower<char>);
	return *str;
}

inline const std::string toLower(const std::string& str)
{
	std::string str_(str);
	return toLower(&str_);
}

inline bool iEquals(ConStrRef a, ConStrRef b)
{
	return std::equal(a.begin(), a.end(), b.begin(), b.end(),
		[](char x, char y) { return ::tolower(x) == ::tolower(y); });
}

inline std::string toString(int n)
{
	std::stringstream ss;
	ss << n;
	return ss.str();
}

inline bool toNumber(ConStrRef str, unsigned long* number)
{
	if (str.empty())
		return false;

	try {
		*number = std::stoul(str);
	}
	catch (...) {
		return false;
	}

	return true;
}

inline void trimLeft(std::string* s)
{
	s->erase(
		s->begin(),
		std::find_if(s->begin(), s->end(), [](int ch) {
			return !::isspace(ch);
		})
	);
}

inline void trimRight(std::string* s)
{
	s->erase(
		std::find_if(s->rbegin(), s->rend(), [](int ch) {
			return !::isspace(ch);
		}).base(),
		s->end()
	);
}

inline void trim(std::string* s)
{
	trimLeft(s);
	trimRight(s);
}

inline std::string trimLeft(ConStrRef s)
{
	std::string s_(s);
	trimLeft(&s_);
	return s_;
}

inline std::string trimRight(ConStrRef s)
{
	std::string s_(s);
	trimRight(&s_);
	return s_;
}

inline std::string trim(ConStrRef s)
{
	std::string s_(s);
	trimLeft(&s_);
	trimRight(&s_);
	return s_;
}

} // namespace StringUtil

namespace StringParser {

class URI
{
public:
	URI() {}

	URI(ConStrRef uri)
	{
		parse(uri);
	}

	bool parse(ConStrRef uri)
	{
		clear();
		std::cmatch result;
		std::regex rule(
			R"((?:([\da-zA-Z-]+)://)?)" // scheme
			R"(([\da-zA-Z\.-]+))" // host
			R"((?::(\d{1,5}))?)" // port
			R"((/(.*))?)" // path
		);
		
		m_valid = std::regex_match(uri.c_str(), result, rule);
		if (m_valid) {
			m_scheme = result[1].str();
			m_host = result[2].str();
			m_port = result[3].str();
			m_path = result[4].str();
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

	HttpUrl(ConStrRef url)
	{
		parse(url);
	}

	bool parse(ConStrRef url)
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

namespace StringEncoder {

inline bool isUriBasicChar(unsigned char ch)
{
	unsigned char t[] = "-_.!~*'()";
	unsigned char* end = t + _sizeof(t) - 1;
	return isalnum(ch)
		|| std::find(t, end, ch) != end;
}

inline bool isUriSeparatorChar(unsigned char ch)
{
	unsigned char t[] = ";/?:@&=+$,#";
	unsigned char* end = t + _sizeof(t) - 1;
	return std::find(t, end, ch) != end;
}

namespace _private {

template <bool tIgnoreSeparatorChar = true>
std::string encodeUriImpl(const std::string& str)
{
	const bool isc = tIgnoreSeparatorChar;
	std::stringstream ss;
	ss << std::hex << std::uppercase;

	for (auto i = str.begin(); i != str.end();) {
		unsigned char ch = *i;

		if (ch == '%') {
			for (int n = 0;
				n < 3 && i != str.end();
				++n) {
				ss << *i;
				++i;
			}
			continue;
		}

		if (isUriBasicChar(ch)
			|| (isc && isUriSeparatorChar(ch)))
			ss << ch;
		else
			ss << '%' << (int)ch;

		++i;
	}

	return ss.str();
}

} // namespace _private

inline std::string encodeUri(ConStrRef str)
{
	return _private::encodeUriImpl<true>(str);
}

inline std::string encodeUriComponent(ConStrRef str)
{
	return _private::encodeUriImpl<false>(str);
}

} // StringEncoder

template <class T>
class MaxMinValue
{
public:
	MaxMinValue(std::initializer_list<T> args)
	{
		m_max = *std::max_element(args.begin(), args.end());
		m_min = *std::min_element(args.begin(), args.end());
	}

	T max() const { return m_max; }
	T min() const { return m_min; }

private:
	T m_max;
	T m_min;
};


class Bool
{
public:
	Bool(bool b = false) : m_b(b) {}
	Bool(BOOL b) : m_b(b != FALSE) {}
	operator bool() const { return m_b; }

private:
	bool m_b;
};

struct BinaryData
{
	static const size_t kBufferSize = KB(2);
	BYTE buffer[kBufferSize];
	DWORD size;
};

class Size
{
public:
	Size() : Size(0, 0) {}
	Size(int w, int h) { set(w, h); }

	int width() const { return m_width; }
	int height() const { return m_height; }

	void width(int w) { m_width = w; }
	void height(int h) { m_height = h; }

	void set(int w, int h)
	{
		m_width = w;
		m_height = h;
	}

	Size operator +(Size rhs) const
	{
		return Size(width() + rhs.width(),
			height() + rhs.height());
	}

	RECT toRect()
	{
		return {0, 0, width(), height()};
	}

private:
	int m_width = 0;
	int m_height = 0;
};

class Point
{
public:
	Point() : Point(0, 0) {}
	Point(int x, int y) { set(x, y); }

	int x() const { return m_x; }
	int y() const { return m_y; }

	void x(int x) { m_x = x; }
	void y(int y) { m_y = y; }

	void xPlus(int n) { m_x += n; }
	void yPlus(int n) { m_y += n; }

	void set(int x, int y)
	{
		m_x = x;
		m_y = y;
	}

private:
	int m_x = 0;
	int m_y = 0;
};

class Rect
{
public:
	Rect(const RECT& rect)
	{
		m_left = rect.left;
		m_top = rect.top;
		m_right = rect.right;
		m_bottom = rect.bottom;
	}

	int left() const { return m_left; }
	int top() const { return m_top; }
	int right() const { return m_right; }
	int bottom() const { return m_bottom; }

	int width() const { return right() - left(); }
	int height() const { return bottom() - top(); }

private:
	int m_left = 0;
	int m_top = 0;
	int m_right = 0;
	int m_bottom = 0;
};

using namespace StringUtil;
using namespace StringEncoder;

END_NAMESPACE_MCD
