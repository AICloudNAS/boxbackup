// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreDaemon.h
//		Purpose: Backup store daemon
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREDAEMON__H
#define BACKUPSTOREDAEMON__H

#include "ServerTLS.h"
#include "BoxPortsAndFiles.h"
#include "BackupConstants.h"
#include "IOStreamGetLine.h"

class BackupStoreAccounts;
class BackupStoreAccountDatabase;
class HousekeepStoreAccount;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreDaemon
//		Purpose: Backup store daemon implementation
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
class BackupStoreDaemon : public ServerTLS<BOX_PORT_BBSTORED>
{
	friend class HousekeepStoreAccount;

public:
	BackupStoreDaemon();
	~BackupStoreDaemon();
private:
	BackupStoreDaemon(const BackupStoreDaemon &rToCopy);
public:

	// For BackupContext to comminicate with housekeeping process
	void SendMessageToHousekeepingProcess(const void *Msg, int MsgLen)
	{
#ifndef WIN32
		mInterProcessCommsSocket.Write(Msg, MsgLen);
#endif
	}

protected:
	
	virtual void SetupInInitialProcess();

	virtual void Run();

	virtual void Connection(SocketStreamTLS &rStream);
	void Connection2(SocketStreamTLS &rStream);
	
	virtual const char *DaemonName() const;
	virtual const char *DaemonBanner() const;

	const ConfigurationVerify *GetConfigVerify() const;
	
	// Housekeeping functions
	void HousekeepingProcess();
	bool CheckForInterProcessMsg(int AccountNum = 0, int MaximumWaitTime = 0);

	void LogConnectionStats(const char *commonName, const SocketStreamTLS &s);

private:
	BackupStoreAccountDatabase *mpAccountDatabase;
	BackupStoreAccounts *mpAccounts;
	bool mExtendedLogging;
	bool mHaveForkedHousekeeping;
	bool mIsHousekeepingProcess;
	bool mHousekeepingInited;

	SocketStream mInterProcessCommsSocket;
	IOStreamGetLine mInterProcessComms;

	virtual void OnIdle();
	void HousekeepingInit();
	void RunHousekeepingIfNeeded();
	int64_t mLastHousekeepingRun;
};


#endif // BACKUPSTOREDAEMON__H

