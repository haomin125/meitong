#ifndef LOGGER_H
#define LOGGER_H

#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <mutex>

#define LogINFO LogMessage(__FILE__, __func__, __LINE__, LogLevel::logINFO)
#define LogDEBUG LogMessage(__FILE__, __func__, __LINE__, LogLevel::logDEBUG)
#define LogWARNING LogMessage(__FILE__, __func__, __LINE__, LogLevel::logWARNING)
#define LogERROR LogMessage(__FILE__, __func__, __LINE__, LogLevel::logERROR)
#define LogCRITICAL LogMessage(__FILE__, __func__, __LINE__, LogLevel::logCRITICAL)
#define ENTRY EntryRaiiObject obj(__FILE__, __func__, __LINE__)

enum class LogLevel : int { logCRITICAL = 0, logERROR = 1, logWARNING = 2, logDEBUG = 3, logINFO = 4 };

class Logger {
public:
	static Logger &instance();
	static void log(const LogLevel level, const std::string &message);

	void setFileName(const std::string &logFile);
	void setLoggerLevel(const LogLevel level);

	bool isLogged(const LogLevel level);
protected:
	static Logger* m_instance;

	LogLevel m_loggerLevel;
	std::string m_sFilename;
	std::ofstream m_fstream;

	friend class Cleanup;
	class Cleanup {
	public:
		~Cleanup();
	};

	std::string logLevel(const LogLevel level);
private:
	Logger();
	virtual ~Logger();

	static std::mutex m_mutex;
};

class LogMessage
{
public:
	// constructor
	// takes identifying info of message.  You can add log level if needed
	LogMessage(const std::string &file, const std::string &function, const int line, const LogLevel level) :
		m_level(level) {
#ifdef __linux__
		m_oss << std::strrchr(file.c_str(), '/') + 1 << "(" << line << "): " << function << "(): ";
#elif _WIN64
		m_oss << std::strrchr(file.c_str(), '\\') + 1 << "(" << line << "): " << function << "(): ";
#endif
	}

	// output operator
	template<typename T>
	LogMessage & operator<<(const T & t) {
		m_oss << t;
		return *this;
	}

	// output message to Logger
	~LogMessage() {
		Logger::log(m_level, m_oss.str());
	}
private:
	LogLevel m_level;
	std::ostringstream m_oss;
};

struct EntryRaiiObject {
	EntryRaiiObject(const char *file, const char *func, const int line) : file_(file), line_(line), func_(func) {
		LogMessage(file_, func_, line_, LogLevel::logDEBUG) << "Entered into " << func_; }
	~EntryRaiiObject() {
		LogMessage(file_, func_, line_, LogLevel::logDEBUG) << "Exited " << func_; }
	const char *file_;
	const int line_;
	const char *func_;
};

#endif // LOGGER_H