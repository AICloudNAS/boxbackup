// --------------------------------------------------------------------------
//
// File
//		Name:    BBStoreDHousekeeping.cpp
//		Purpose: Implementation of housekeeping functions for bbstored
//		Created: 11/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
#endif

#include "BackupStoreDaemon.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreAccounts.h"
#include "HousekeepStoreAccount.h"
#include "BoxTime.h"
#include "Configuration.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::HousekeepingProcess()
//		Purpose: Do housekeeping
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
void BackupStoreDaemon::HousekeepingInit()
{

	mLastHousekeepingRun = 0;
}

void BackupStoreDaemon::HousekeepingProcess()
{
	HousekeepingInit();

	// Get the time between housekeeping runs
	const Configuration &rconfig(GetConfiguration());
	int64_t housekeepingInterval = SecondsToBoxTime(rconfig.GetKeyValueInt("TimeBetweenHousekeeping"));

	while(!StopRun())
	{
		RunHousekeepingIfNeeded();

		// Calculate how long should wait before doing the next 
		// housekeeping run
		int64_t timeNow = GetCurrentBoxTime();
		time_t secondsToGo = BoxTimeToSeconds(
			(mLastHousekeepingRun + housekeepingInterval) - 
			timeNow);
		if(secondsToGo < 1) secondsToGo = 1;
		if(secondsToGo > 60) secondsToGo = 60;
		int32_t millisecondsToGo = ((int)secondsToGo) * 1000;
	
		// Check to see if there's any message pending
		CheckForInterProcessMsg(0 /* no account */, millisecondsToGo);
	}
}

void BackupStoreDaemon::RunHousekeepingIfNeeded()
{
	// Get the time between housekeeping runs
	const Configuration &rconfig(GetConfiguration());
	int64_t housekeepingInterval = SecondsToBoxTime(rconfig.GetKeyValueInt("TimeBetweenHousekeeping"));

	// Time now
	int64_t timeNow = GetCurrentBoxTime();

	// Do housekeeping if the time interval has elapsed since the last check
	if((timeNow - mLastHousekeepingRun) < housekeepingInterval)
	{
		return;
	}

	// Store the time
	mLastHousekeepingRun = timeNow;
	::syslog(LOG_INFO, "Starting housekeeping");

	// Get the list of accounts
	std::vector<int32_t> accounts;
	if(mpAccountDatabase)
	{
		mpAccountDatabase->GetAllAccountIDs(accounts);
	}
			
	SetProcessTitle("housekeeping, active");
			
	// Check them all
	for(std::vector<int32_t>::const_iterator i = accounts.begin(); i != accounts.end(); ++i)
	{
		try
		{
			if(mpAccounts)
			{
				// Get the account root
				std::string rootDir;
				int discSet = 0;
				mpAccounts->GetAccountRoot(*i, rootDir, discSet);
				
				// Do housekeeping on this account
				HousekeepStoreAccount housekeeping(*i, rootDir, discSet, *this);
				housekeeping.DoHousekeeping();
			}
		}
		catch(BoxException &e)
		{
			::syslog(LOG_ERR, "while housekeeping account %08X, exception %s (%d/%d) -- aborting housekeeping run for this account",
				*i, e.what(), e.GetType(), e.GetSubType());
		}
		catch(std::exception &e)
		{
			::syslog(LOG_ERR, "while housekeeping account %08X, exception %s -- aborting housekeeping run for this account",
				*i, e.what());
		}
		catch(...)
		{
			::syslog(LOG_ERR, "while housekeeping account %08X, unknown exception -- aborting housekeeping run for this account",
				*i);
		}
	
		int64_t timeNow = GetCurrentBoxTime();
		time_t secondsToGo = BoxTimeToSeconds(
			(mLastHousekeepingRun + housekeepingInterval) - 
			timeNow);
		if(secondsToGo < 1) secondsToGo = 1;
		if(secondsToGo > 60) secondsToGo = 60;
		int32_t millisecondsToGo = ((int)secondsToGo) * 1000;

		// Check to see if there's any message pending
		CheckForInterProcessMsg(0 /* no account */, millisecondsToGo);

		// Stop early?
		if(StopRun())
		{
			break;
		}
	}
		
	::syslog(LOG_INFO, "Finished housekeeping");

	// Placed here for accuracy, if StopRun() is true, for example.
	SetProcessTitle("housekeeping, idle");
}

void BackupStoreDaemon::OnIdle()
{
	#ifdef WIN32
	if (!mHousekeepingInited)
	{
		HousekeepingInit();
		mHousekeepingInited = true;
	}

	RunHousekeepingIfNeeded();
	#endif
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::CheckForInterProcessMsg(int, int)
//		Purpose: Process a message, returning true if the housekeeping process
//				 should abort for the specified account.
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
bool BackupStoreDaemon::CheckForInterProcessMsg(int AccountNum, int MaximumWaitTime)
{
	if(!mInterProcessCommsSocket.IsOpened())
	{
		return false;
	}

	// First, check to see if it's EOF -- this means something has gone wrong, and the housekeeping should terminate.
	if(mInterProcessComms.IsEOF())
	{
		SetTerminateWanted();
		return true;
	}

	// Get a line, and process the message
	std::string line;
	if(mInterProcessComms.GetLine(line, false /* no pre-processing */, MaximumWaitTime))
	{
		TRACE1("housekeeping received command '%s' over interprocess comms\n", line.c_str());
	
		int account = 0;
	
		if(line == "h")
		{
			// HUP signal received by main process
			SetReloadConfigWanted();
			return true;
		}
		else if(line == "t")
		{
			// Terminate signal received by main process
			SetTerminateWanted();
			return true;
		}
		else if(sscanf(line.c_str(), "r%x", &account) == 1)
		{
			// Main process is trying to lock an account -- are we processing it?
			if(account == AccountNum)
			{
				// Yes! -- need to stop now so when it retries to get the lock, it will succeed
				::syslog(LOG_INFO, "Housekeeping giving way to connection for account 0x%08x", AccountNum);
				return true;
			}
		}
	}
	
	return false;
}


