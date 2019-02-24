#pragma once
#include "base.h"


namespace ward
{

class Result
{
public:
	static const int kSuccess = 0;

	Result(const char* space, int code) :
		m_space(space), m_code(code) {}

	Result() : Result("", kSuccess) {}

	bool ok() { return m_code == kSuccess; }
	bool failed() { return !ok(); }

private:
	const char* m_space;
	int m_code;
};

class InternalError {
	static Result make(int code) { return {"intetnal", code}; }

public:
	static Result assertFailed() { return make(1); }
	static Result invalidInput() { return make(2); }
};

class FeatureError {
	static Result make(int code) { return { "feature", code }; }

public:
	static Result httpBodyOver2GB() { return make(1); }
};

class RequireError {
	static Result make(int code) { return { "require", code }; }

public:
	static Result httpSupportRange() { return make(1); }
};


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
	typedef AssertHelper& SelfRef;

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

	SelfRef setContext()
	{
		return *this;
	}

	template <class V1>
	SelfRef setContext(V1 v1)
	{
		*this << v1;
		return *this;
	}

	template <class V1, class V2>
	SelfRef setContext(V1 v1, V2 v2)
	{
		*this << v1 << v2;
		return *this;
	}

	template <class V1, class V2, class V3>
	SelfRef setContext(V1 v1, V2 v2, V3 v3)
	{
		*this << v1 << v2 << v3;
		return *this;
	}

	operator bool () const { return m_cond;	}

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

#define _assert_helper(level, cond) \
	AssertHelper(level, SRC_CONTEXT, cond, SRC_STATEMENT(cond))
#define _eval_warn(cond) _assert_helper(0, cond)
#define _eval_error(cond) _assert_helper(1, cond)

#define _should(statement, ...) \
	_eval_warn(statement).setContext(__VA_ARGS__)

#define _must_or_return(err, statement, ...) \
	if (!_eval_error(statement).setContext(__VA_ARGS__)) \
		return err();

#define _must(statement, ...) \
	_must_or_return(InternalError::assertFailed, statement, __VA_ARGS__)

#define _must_not(statement, ...) \
	_must(!(statement), __VA_ARGS__)

#define _call(result) { \
	Result&& r = result; \
	if (r.failed()) \
		return r; \
}


} // namespace ward

using namespace ward;
