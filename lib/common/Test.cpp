// --------------------------------------------------------------------------
//
// File
//		Name:    Test.cpp
//		Purpose: Useful stuff for tests
//		Created: 2008/04/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include "Test.h"

bool TestFileExists(const char *Filename)
{
	EMU_STRUCT_STAT st;
	return EMU_STAT(Filename, &st) == 0 && (st.st_mode & S_IFDIR) == 0;
}

bool TestFileNotEmpty(const char *Filename)
{
	EMU_STRUCT_STAT st;
	return EMU_STAT(Filename, &st) == 0 && (st.st_mode & S_IFDIR) == 0 &&
		st.st_size > 0;
}

bool TestDirExists(const char *Filename)
{
	EMU_STRUCT_STAT st;
	return EMU_STAT(Filename, &st) == 0 && (st.st_mode & S_IFDIR) == S_IFDIR;
}

// -1 if doesn't exist
int TestGetFileSize(const std::string& Filename)
{
	EMU_STRUCT_STAT st;
	if(EMU_STAT(Filename.c_str(), &st) == 0)
	{
		return st.st_size;
	}
	return -1;
}

std::string ConvertPaths(const std::string& rOriginal)
{
#ifdef WIN32
	// convert UNIX paths to native

	std::string converted;
	for (size_t i = 0; i < rOriginal.size(); i++)
	{
		if (rOriginal[i] == '/')
		{
			converted += '\\';
		}
		else
		{
			converted += rOriginal[i];
		}
	}
	return converted;

#else // !WIN32
	return rOriginal;
#endif
}

int RunCommand(const std::string& rCommandLine)
{
	return ::system(ConvertPaths(rCommandLine).c_str());
}

#ifdef WIN32
#include <windows.h>
#endif

bool ServerIsAlive(int pid)
{
	#ifdef WIN32

		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION,
			false, pid);
		if (hProcess == NULL)
		{
			if (GetLastError() != ERROR_INVALID_PARAMETER)
			{
				BOX_ERROR("Failed to open process " << pid <<
					": " <<
					GetErrorMessage(GetLastError()));
			}
			return false;
		}

		DWORD exitCode;
		BOOL result = GetExitCodeProcess(hProcess, &exitCode);
		CloseHandle(hProcess);

		if (result == 0)
		{
			BOX_ERROR("Failed to get exit code for process " <<
				pid << ": " <<
				GetErrorMessage(GetLastError()))
			return false;
		}

		if (exitCode == STILL_ACTIVE)
		{
			return true;
		}
		
		return false;

	#else // !WIN32

		if(pid == 0) return false;
		return ::kill(pid, 0) != -1;

	#endif // WIN32
}

int ReadPidFile(const char *pidFile)
{
	if(!TestFileNotEmpty(pidFile))
	{
		TEST_FAIL_WITH_MESSAGE("Server didn't save PID file "
			"(perhaps one was already running?)");	
		return -1;
	}
	
	int pid = -1;

	FILE *f = fopen(pidFile, "r");
	if(f == NULL || fscanf(f, "%d", &pid) != 1)
	{
		TEST_FAIL_WITH_MESSAGE("Couldn't read PID file");	
		return -1;
	}
	fclose(f);
	
	return pid;
}

int LaunchServer(const std::string& rCommandLine, const char *pidFile)
{
	::fprintf(stdout, "Starting server: %s\n", rCommandLine.c_str());

#ifdef WIN32

	PROCESS_INFORMATION procInfo;

	STARTUPINFO startInfo;
	startInfo.cb = sizeof(startInfo);
	startInfo.lpReserved = NULL;
	startInfo.lpDesktop  = NULL;
	startInfo.lpTitle    = NULL;
	startInfo.dwFlags = 0;
	startInfo.cbReserved2 = 0;
	startInfo.lpReserved2 = NULL;

	std::string cmd = ConvertPaths(rCommandLine);
	CHAR* tempCmd = strdup(cmd.c_str());

	DWORD result = CreateProcess
	(
		NULL,        // lpApplicationName, naughty!
		tempCmd,     // lpCommandLine
		NULL,        // lpProcessAttributes
		NULL,        // lpThreadAttributes
		false,       // bInheritHandles
		0,           // dwCreationFlags
		NULL,        // lpEnvironment
		NULL,        // lpCurrentDirectory
		&startInfo,  // lpStartupInfo
		&procInfo    // lpProcessInformation
	);

	free(tempCmd);

	if (result == 0)
	{
		DWORD err = GetLastError();
		printf("Launch failed: %s: error %d\n", rCommandLine.c_str(),
			(int)err);
		TEST_FAIL_WITH_MESSAGE("Couldn't start server");
		return -1;
	}

	CloseHandle(procInfo.hProcess);
	CloseHandle(procInfo.hThread);

	return WaitForServerStartup(pidFile, (int)procInfo.dwProcessId);

#else // !WIN32

	if(RunCommand(rCommandLine) != 0)
	{
		TEST_FAIL_WITH_MESSAGE("Couldn't start server");
		return -1;
	}

	return WaitForServerStartup(pidFile, 0);

#endif // WIN32
}

int WaitForServerStartup(const char *pidFile, int pidIfKnown)
{
	#ifdef WIN32
	if (pidFile == NULL)
	{
		return pidIfKnown;
	}
	#else
	// on other platforms there is no other way to get 
	// the PID, so a NULL pidFile doesn't make sense.
	ASSERT(pidFile != NULL);
	#endif

	// time for it to start up
	if (Logging::GetGlobalLevel() >= Log::TRACE)
	{
		BOX_TRACE("Waiting for server to start");
	}
	else
	{
		::fprintf(stdout, "Waiting for server to start: ");
	}

	for (int i = 0; i < 15; i++)
	{
		if (TestFileNotEmpty(pidFile))	
		{
			break;
		}

		if (pidIfKnown && !ServerIsAlive(pidIfKnown))
		{
			break;
		}

		if (Logging::GetGlobalLevel() < Log::TRACE)
		{
			::fprintf(stdout, ".");
			::fflush(stdout);
		}

		::sleep(1);
	}

	// on Win32 we can check whether the process is alive
	// without even checking the PID file

	if (pidIfKnown && !ServerIsAlive(pidIfKnown))
	{
		if (Logging::GetGlobalLevel() >= Log::TRACE)
		{
			BOX_ERROR("server died!");
		}
		else
		{
			::fprintf(stdout, " server died!\n");
		}

		TEST_FAIL_WITH_MESSAGE("Server died!");	
		return -1;
	}

	if (!TestFileNotEmpty(pidFile))
	{
		if (Logging::GetGlobalLevel() >= Log::TRACE)
		{
			BOX_ERROR("timed out!");
		}
		else
		{
			::fprintf(stdout, " timed out!\n");
		}

		TEST_FAIL_WITH_MESSAGE("Server didn't save PID file");	
		return -1;
	}

	if (Logging::GetGlobalLevel() >= Log::TRACE)
	{
		BOX_TRACE("Server started");
	}
	else
	{
		::fprintf(stdout, " done.\n");
	}

	// wait a second for the pid to be written to the file
	::sleep(1);

	// read pid file
	int pid = ReadPidFile(pidFile);

	// On Win32 we can check whether the PID in the pidFile matches
	// the one returned by the system, which it always should.

	if (pidIfKnown && pid != pidIfKnown)
	{
		BOX_ERROR("Server wrote wrong pid to file (" << pidFile <<
			"): expected " << pidIfKnown << " but found " <<
			pid);
		TEST_FAIL_WITH_MESSAGE("Server wrote wrong pid to file");	
		return -1;
	}

	return pid;
}

void TestRemoteProcessMemLeaksFunc(const char *filename,
	const char* file, int line)
{
#ifdef BOX_MEMORY_LEAK_TESTING
	// Does the file exist?
	if(!TestFileExists(filename))
	{
		if (failures == 0)
		{
			first_fail_file = file;
			first_fail_line = line;
		}
		++failures;
		printf("FAILURE: MemLeak report not available (file %s) "
			"at %s:%d\n", filename, file, line);
	}
	else
	{
		// Is it empty?
		if(TestGetFileSize(filename) > 0)
		{
			if (failures == 0)
			{
				first_fail_file = file;
				first_fail_line = line;
			}
			++failures;
			printf("FAILURE: Memory leaks found in other process "
				"(file %s) at %s:%d\n==========\n", 
				filename, file, line);
			FILE *f = fopen(filename, "r");
			char linebuf[512];
			while(::fgets(linebuf, sizeof(linebuf), f) != 0)
			{
				printf("%s", linebuf);
			}
			fclose(f);
			printf("==========\n");
		}
		
		// Delete it
		::unlink(filename);
	}
#endif
}

void force_sync()
{
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"force-sync") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");
}

void wait_for_sync_start()
{
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"wait-for-sync") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");
}

void wait_for_sync_end()
{
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"wait-for-end") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");
}

void sync_and_wait()
{
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"sync-and-wait") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");
}

void terminate_bbackupd(int pid)
{
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"terminate") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");

	for (int i = 0; i < 20; i++)
	{
		if (!ServerIsAlive(pid)) break;
		fprintf(stdout, ".");
		fflush(stdout);
		sleep(1);
	}

	TEST_THAT(!ServerIsAlive(pid));
	TestRemoteProcessMemLeaks("bbackupd.memleaks");
}


// Wait a given number of seconds for something to complete
void wait_for_operation(int seconds, const char* message)
{
	if (Logging::GetGlobalLevel() >= Log::TRACE)
	{
		BOX_TRACE("Waiting " << seconds << " seconds for " << message);
	}
	else
	{
		printf("Waiting for %s: ", message);
		fflush(stdout);
	}

	for(int l = 0; l < seconds; ++l)
	{
		sleep(1);
		if (Logging::GetGlobalLevel() < Log::TRACE)
		{
			printf(".");
			fflush(stdout);
		}
	}

	if (Logging::GetGlobalLevel() >= Log::TRACE)
	{
		BOX_TRACE("Finished waiting for " << message);
	}
	else
	{
		printf(" done.\n");
		fflush(stdout);
	}
}

void safe_sleep(int seconds)
{
	BOX_TRACE("sleeping for " << seconds << " seconds");

#ifdef WIN32
	Sleep(seconds * 1000);
#else
	struct timespec ts;
	memset(&ts, 0, sizeof(ts));
	ts.tv_sec  = seconds;
	ts.tv_nsec = 0;
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
	{
		// FIXME evil hack for OSX, where ts.tv_sec contains
		// a negative number interpreted as unsigned 32-bit
		// when nanosleep() returns later than expected.

		int32_t secs = (int32_t) ts.tv_sec;
		int64_t remain_ns = (secs * 1000000000) + ts.tv_nsec;

		if (remain_ns < 0)
		{
			BOX_WARNING("nanosleep interrupted " <<
				((float)(0 - remain_ns) / 1000000000) <<
				" secs late");
			return;
		}

		BOX_TRACE("nanosleep interrupted with " <<
			(remain_ns / 1000000000) << " secs remaining, "
			"sleeping again");
	}
#endif
}

