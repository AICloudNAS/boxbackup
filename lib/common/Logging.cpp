// --------------------------------------------------------------------------
//
// File
//		Name:    Logging.cpp
//		Purpose: Generic logging core routines implementation
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <errno.h>
#include <time.h>
#include <string.h> // for stderror

// c.f. http://bugs.debian.org/512510
#include <cstdio>

#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
#endif
#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif
#ifdef WIN32
	#include <process.h>
#endif

#include <cstring>
#include <iomanip>

#include "BoxTime.h"
#include "Logging.h"

bool Logging::sLogToSyslog  = false;
bool Logging::sLogToConsole = false;
bool Logging::sContextSet   = false;

bool HideExceptionMessageGuard::sHiddenState = false;

std::vector<Logger*> Logging::sLoggers;
std::string Logging::sContext;
Console*    Logging::spConsole = NULL;
Syslog*     Logging::spSyslog  = NULL;
Log::Level  Logging::sGlobalLevel = Log::EVERYTHING;
Logging     Logging::sGlobalLogging; //automatic initialisation
std::string Logging::sProgramName;

HideSpecificExceptionGuard::SuppressedExceptions_t
	HideSpecificExceptionGuard::sSuppressedExceptions;

int Logging::Guard::sGuardCount = 0;
Log::Level Logging::Guard::sOriginalLevel = Log::INVALID;

Logging::Logging()
{
	ASSERT(!spConsole);
	ASSERT(!spSyslog);
	spConsole = new Console();
	spSyslog  = new Syslog();
	sLogToConsole = true;
	sLogToSyslog  = true;
}

Logging::~Logging()
{
	sLogToConsole = false;
	sLogToSyslog  = false;
	delete spConsole;
	delete spSyslog;
	spConsole = NULL;
	spSyslog  = NULL;
}

void Logging::ToSyslog(bool enabled)
{
	if (!sLogToSyslog && enabled)
	{
		Add(spSyslog);
	}
	
	if (sLogToSyslog && !enabled)
	{
		Remove(spSyslog);
	}
	
	sLogToSyslog = enabled;
}

void Logging::ToConsole(bool enabled)
{
	if (!sLogToConsole && enabled)
	{
		Add(spConsole);
	}
	
	if (sLogToConsole && !enabled)
	{
		Remove(spConsole);
	}
	
	sLogToConsole = enabled;
}

void Logging::FilterConsole(Log::Level level)
{
	spConsole->Filter(level);
}

void Logging::FilterSyslog(Log::Level level)
{
	spSyslog->Filter(level);
}

void Logging::Add(Logger* pNewLogger)
{
	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		if (*i == pNewLogger)
		{
			return;
		}
	}
	
	sLoggers.insert(sLoggers.begin(), pNewLogger);
}

void Logging::Remove(Logger* pOldLogger)
{
	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		if (*i == pOldLogger)
		{
			sLoggers.erase(i);
			return;
		}
	}
}

void Logging::Log(Log::Level level, const std::string& rFile, 
	int line, const std::string& rMessage)
{
	if (level > sGlobalLevel)
	{
		return;
	}

	std::string newMessage;
	
	if (sContextSet)
	{
		newMessage += "[" + sContext + "] ";
	}
	
	newMessage += rMessage;
	
	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		bool result = (*i)->Log(level, rFile, line, newMessage);
		if (!result)
		{
			return;
		}
	}
}

void Logging::LogToSyslog(Log::Level level, const std::string& rFile, 
	int line, const std::string& rMessage)
{
	if (!sLogToSyslog)
	{
		return;
	}

	if (level > sGlobalLevel)
	{
		return;
	}

	std::string newMessage;
	
	if (sContextSet)
	{
		newMessage += "[" + sContext + "] ";
	}
	
	newMessage += rMessage;

	spSyslog->Log(level, rFile, line, newMessage);
}

void Logging::SetContext(std::string context)
{
	sContext = context;
	sContextSet = true;
}

Log::Level Logging::GetNamedLevel(const std::string& rName)
{
	if      (rName == "nothing") { return Log::NOTHING; }
	else if (rName == "fatal")   { return Log::FATAL; }
	else if (rName == "error")   { return Log::ERROR; }
	else if (rName == "warning") { return Log::WARNING; }
	else if (rName == "notice")  { return Log::NOTICE; }
	else if (rName == "info")    { return Log::INFO; }
	else if (rName == "trace")   { return Log::TRACE; }
	else if (rName == "everything") { return Log::EVERYTHING; }
	else
	{
		BOX_ERROR("Unknown verbosity level: " << rName);
		return Log::INVALID;
	}
}

void Logging::ClearContext()
{
	sContextSet = false;
}

void Logging::SetProgramName(const std::string& rProgramName)
{
	sProgramName = rProgramName;

	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		(*i)->SetProgramName(rProgramName);
	}
}

void Logging::SetFacility(int facility)
{
	spSyslog->SetFacility(facility);
}

Logger::Logger() 
: mCurrentLevel(Log::EVERYTHING) 
{
	Logging::Add(this);
}

Logger::Logger(Log::Level Level) 
: mCurrentLevel(Level) 
{
	Logging::Add(this);
}

Logger::~Logger() 
{
	Logging::Remove(this);
}

bool Logger::IsEnabled(Log::Level level)
{
	return Logging::IsEnabled(level) &&
		(int)mCurrentLevel >= (int)level;
}

bool Console::sShowTime = false;
bool Console::sShowTimeMicros = false;
bool Console::sShowTag = false;
bool Console::sShowPID = false;
std::string Console::sTag;

void Console::SetProgramName(const std::string& rProgramName)
{
	sTag = rProgramName;
}

void Console::SetShowTag(bool enabled)
{
	sShowTag = enabled;
}

void Console::SetShowTime(bool enabled)
{
	sShowTime = enabled;
}

void Console::SetShowTimeMicros(bool enabled)
{
	sShowTimeMicros = enabled;
}

void Console::SetShowPID(bool enabled)
{
	sShowPID = enabled;
}

bool Console::Log(Log::Level level, const std::string& rFile, 
	int line, std::string& rMessage)
{
	if (level > GetLevel())
	{
		return true;
	}
	
	FILE* target = stdout;
	
	if (level <= Log::WARNING)
	{
		target = stderr;
	}

	std::ostringstream buf;

	if (sShowTime)
	{
		buf << FormatTime(GetCurrentBoxTime(), false, sShowTimeMicros);
		buf << " ";
	}

	if (sShowTag)
	{
		if (sShowPID)
		{
			buf << "[" << sTag << " " << getpid() << "] ";
		}
		else
		{
			buf << "[" << sTag << "] ";
		}
	}
	else if (sShowPID)
	{
		buf << "[" << getpid() << "] ";
	}

	if (level <= Log::FATAL)
	{
		buf << "FATAL:   ";
	}
	else if (level <= Log::ERROR)
	{
		buf << "ERROR:   ";
	}
	else if (level <= Log::WARNING)
	{
		buf << "WARNING: ";
	}
	else if (level <= Log::NOTICE)
	{
		buf << "NOTICE:  ";
	}
	else if (level <= Log::INFO)
	{
		buf << "INFO:    ";
	}
	else if (level <= Log::TRACE)
	{
		buf << "TRACE:   ";
	}

	buf << rMessage;

	#ifdef WIN32
		std::string output = buf.str();
		if(ConvertUtf8ToConsole(output.c_str(), output) == false)
		{
			fprintf(target, "%s (and failed to convert to console encoding)\n",
				output.c_str());
		}
		else
		{
			fprintf(target, "%s\n", output.c_str());
		}
	#else
		fprintf(target, "%s\n", buf.str().c_str());
	#endif

	fflush(target);
	
	return true;
}

bool Syslog::Log(Log::Level level, const std::string& rFile, 
	int line, std::string& rMessage)
{
	if (level > GetLevel())
	{
		return true;
	}
	
	int syslogLevel = LOG_ERR;
	
	switch(level)
	{
		case Log::NOTHING:    /* fall through */
		case Log::INVALID:    /* fall through */
		case Log::FATAL:      syslogLevel = LOG_CRIT;    break;
		case Log::ERROR:      syslogLevel = LOG_ERR;     break;
		case Log::WARNING:    syslogLevel = LOG_WARNING; break;
		case Log::NOTICE:     syslogLevel = LOG_NOTICE;  break;
		case Log::INFO:       syslogLevel = LOG_INFO;    break;
		case Log::TRACE:      /* fall through */
		case Log::EVERYTHING: syslogLevel = LOG_DEBUG;   break;
	}

	std::string msg;

	if (level <= Log::FATAL)
	{
		msg = "FATAL: ";
	}
	else if (level <= Log::ERROR)
	{
		msg = "ERROR: ";
	}
	else if (level <= Log::WARNING)
	{
		msg = "WARNING: ";
	}
	else if (level <= Log::NOTICE)
	{
		msg = "NOTICE: ";
	}

	msg += rMessage;

	syslog(syslogLevel, "%s", msg.c_str());
	
	return true;
}

Syslog::Syslog() : mFacility(LOG_LOCAL6)
{
	::openlog("Box Backup", LOG_PID, mFacility);
}

Syslog::~Syslog()
{
	::closelog();
}

void Syslog::SetProgramName(const std::string& rProgramName)
{
	mName = rProgramName;
	::closelog();
	::openlog(mName.c_str(), LOG_PID, mFacility);
}

void Syslog::SetFacility(int facility)
{
	mFacility = facility;
	::closelog();
	::openlog(mName.c_str(), LOG_PID, mFacility);
}

int Syslog::GetNamedFacility(const std::string& rFacility)
{
	#define CASE_RETURN(x) if (rFacility == #x) { return LOG_ ## x; }
	CASE_RETURN(LOCAL0)
	CASE_RETURN(LOCAL1)
	CASE_RETURN(LOCAL2)
	CASE_RETURN(LOCAL3)
	CASE_RETURN(LOCAL4)
	CASE_RETURN(LOCAL5)
	CASE_RETURN(LOCAL6)
	CASE_RETURN(DAEMON)
	#undef CASE_RETURN

	BOX_ERROR("Unknown log facility '" << rFacility << "', "
		"using default LOCAL6");
	return LOG_LOCAL6;
}

bool FileLogger::Log(Log::Level Level, const std::string& rFile, 
	int line, std::string& rMessage)
{
	if (mLogFile.StreamClosed())
	{
		/* skip this logger to allow logging failure to open
		the log file, without causing an infinite loop */
		return true;
	}

	if (Level > GetLevel())
	{
		return true;
	}
	
	/* avoid infinite loop if this throws an exception */
	Log::Level oldLevel = GetLevel();
	Filter(Log::NOTHING);

	std::ostringstream buf;
	buf << FormatTime(GetCurrentBoxTime(), true, false);
	buf << " ";

	if (Level <= Log::FATAL)
	{
		buf << "[FATAL]   ";
	}
	else if (Level <= Log::ERROR)
	{
		buf << "[ERROR]   ";
	}
	else if (Level <= Log::WARNING)
	{
		buf << "[WARNING] ";
	}
	else if (Level <= Log::NOTICE)
	{
		buf << "[NOTICE]  ";
	}
	else if (Level <= Log::INFO)
	{
		buf << "[INFO]    ";
	}
	else if (Level <= Log::TRACE)
	{
		buf << "[TRACE]   ";
	}

	buf << rMessage << "\n";
	std::string output = buf.str();

	#ifdef WIN32
		ConvertUtf8ToConsole(output.c_str(), output);
	#endif

	mLogFile.Write(output.c_str(), output.length());

	// no infinite loop, reset to saved logging level
	Filter(oldLevel);
	return true;
}

std::string PrintEscapedBinaryData(const std::string& rInput)
{
	std::ostringstream output;

	for (size_t i = 0; i < rInput.length(); i++)
	{
		if (isprint(rInput[i]))
		{
			output << rInput[i];
		}
		else
		{
			output << "\\x" << std::hex << std::setw(2) <<
				std::setfill('0') << (int) rInput[i] <<
				std::dec;
		}
	}

	return output.str();
}

bool HideSpecificExceptionGuard::IsHidden(int type, int subtype)
{
	for (SuppressedExceptions_t::iterator
		i  = sSuppressedExceptions.begin();
		i != sSuppressedExceptions.end(); i++)
	{
		if(i->first == type && i->second == subtype)
		{
			return true;
		}
	}
	return false;
}

