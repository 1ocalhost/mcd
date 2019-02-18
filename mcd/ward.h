#pragma once
#include "base.h"

namespace ward
{
class IMetaViewer
{
public:
	virtual std::string var_dump() const = 0;
};

#define MetaViewerFunc std::string var_dump() const override


class VarDumper
{
public:
	VarDumper(bool opened = true, const char* wrapper = "{}") :
		m_opened(opened), m_wrapper(wrapper)
	{
		if (opened)
			m_view.reset(new std::string());
	}

	std::string view() const
	{
		std::string str;
		bool validWrapper = strlen(m_wrapper) >= 2;
		if (validWrapper)
			str.push_back(m_wrapper[0]);

		if (m_view.get())
			str.append(*m_view);

		if (validWrapper)
			str.push_back(m_wrapper[1]);

		StringUtil::escapeChar(&str);
		return str;
	}

	operator std::string() const
	{
		return view();
	}

	VarDumper& operator <<(const IMetaViewer* data)
	{
		if (!m_opened)
			return *this;

		return push_var(data->var_dump());
	}

	VarDumper& operator <<(const IMetaViewer& data)
	{
		return *this << (&data);
	}

	VarDumper& operator <<(const std::string& data)
	{
		return push_var(data, true);
	}

	VarDumper& operator <<(bool data)
	{
		return push_var(data ? "true" : "false");
	}

	// avoid string literal is parsed as array 
	VarDumper& operator <<(const char* data)
	{
		return *this << std::string(data);
	}

	template <class T, class U>
	VarDumper& operator <<(const std::pair<T, U> data)
	{
		std::string value;
		value = (VarDumper(true, "") << data.first);
		value.append(":");
		value.append(VarDumper(true, "") << data.second);
		return push_var(value);
	}

	template <class T>
	VarDumper& operator <<(const std::vector<T>& data)
	{
		return parseArray(data);
	}

	template <class Type, size_t n>
	VarDumper& operator <<(Type(&data)[n])
	{
		return parseArray(data);
	}

	template <class T, class U>
	VarDumper& operator <<(const std::map<T, U>& data)
	{
		if (!m_opened)
			return *this;

		VarDumper dump(true);
		for (const auto& i : data)
			dump << i;

		push_var(dump.view());
		return *this;
	}

#define VarDumper_def_view_num_func(type) \
	VarDumper& operator <<(type data) { \
		return push_var(std::to_string(data)); \
	}

	VarDumper_def_view_num_func(int)
	VarDumper_def_view_num_func(long)
	VarDumper_def_view_num_func(long long)
	VarDumper_def_view_num_func(unsigned)
	VarDumper_def_view_num_func(unsigned long)
	VarDumper_def_view_num_func(unsigned long long)
	VarDumper_def_view_num_func(float)
	VarDumper_def_view_num_func(double)
	VarDumper_def_view_num_func(long double)

#undef VarDumper_def_view_num_func

private:
	VarDumper& push_var(const std::string& var, bool isString = false)
	{
		if (!m_opened)
			return *this;

		if (m_view->size())
			m_view->append(", ");

		if (isString)
			m_view->push_back('"');

		m_view->append(var);

		if (isString)
			m_view->push_back('"');

		return *this;
	}

	template <class T>
	VarDumper& parseArray(const T& data)
	{
		if (!m_opened)
			return *this;

		VarDumper dump(true, "[]");
		for (const auto& i : data)
			dump << i;

		push_var(dump.view());
		return *this;
	}

	bool m_opened;
	const char* m_wrapper;
	std::unique_ptr<std::string> m_view;
};


class AssertSrcContext
{
public:
	AssertSrcContext(const char* file, int line, const char* func) :
		m_file(file), m_line(line), m_func(func)
	{
		m_file = PathFindFileNameA(m_file);
	}

	std::string str() const
	{
		std::ostringstream ss;
		ss << m_file << "(:" << m_line << "): " << m_func;
		return ss.str();
	}

private:
	const char* m_file;
	int m_line;
	const char* m_func;
};


class AssertHelper : public VarDumper
{
public:
	AssertHelper(int type, AssertSrcContext context, bool cond,
		const char* statement = NULL) :
		VarDumper(!cond), m_type(type), m_srcContext(context),
		m_cond(cond), m_statement(statement)
	{
		m_err = GetLastError();
		if (debugMode() && type == 1 && !cond)
			DebugBreak();
	}

	~AssertHelper()
	{
		if (m_cond)
			return;

		format();
		if (debugMode())
			OutputDebugStringA(m_formated.c_str());
	}

private:
	void format()
	{
		std::ostringstream ss;
		if (debugMode())
			ss << "_ _ _ _ _ _ ";

		ss << "<" << time(nullptr) << ": "
			<< GetCurrentThreadId() << "> "
			<< m_srcContext.str() << ": ";

		if (m_statement)
			ss << "\"" << m_statement << "\": ";

		ss << "[" << m_type << ", "
			<< m_err << "] "
			<< view() << "\n";

		m_formated = ss.str();
	}

	std::string m_formated;
	int m_err;
	AssertSrcContext m_srcContext;
	int m_type;
	bool m_cond;
	const char* m_statement;
};

#define SRC_CONTEXT AssertSrcContext(__FILE__, __LINE__, __FUNCTION__)
#ifdef _DEBUG
#define SRC_STATEMENT(x) #x
#else
#define SRC_STATEMENT(x) NULL
#endif

#define _should(cond) AssertHelper(0, SRC_CONTEXT, cond, SRC_STATEMENT(cond))
#define _should_not(cond) AssertHelper(0, SRC_CONTEXT, !(cond), SRC_STATEMENT(cond))
#define _must(cond) AssertHelper(1, SRC_CONTEXT, cond, SRC_STATEMENT(cond))
#define _must_not(cond) AssertHelper(1, SRC_CONTEXT, !(cond), SRC_STATEMENT(cond))

} // namespace ward

using namespace ward;
