// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreAccounts.h
//		Purpose: Account management for backup store server
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREACCOUNTS__H
#define BACKUPSTOREACCOUNTS__H

#include <string>

#include "BackupStoreAccountDatabase.h"
#include "BackupAccountControl.h"
#include "NamedLock.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreAccounts
//		Purpose: Account management for backup store server
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
class BackupStoreAccounts
{
public:
	BackupStoreAccounts(BackupStoreAccountDatabase &rDatabase);
	~BackupStoreAccounts();
private:
	BackupStoreAccounts(const BackupStoreAccounts &rToCopy);

public:
	void Create(int32_t ID, int DiscSet, int64_t SizeSoftLimit,
		int64_t SizeHardLimit, const std::string &rAsUsername);

	bool AccountExists(int32_t ID);
	void GetAccountRoot(int32_t ID, std::string &rRootDirOut, int &rDiscSetOut) const;
	static std::string GetAccountRoot(const
		BackupStoreAccountDatabase::Entry &rEntry)
	{
		return MakeAccountRootDir(rEntry.GetID(), rEntry.GetDiscSet());
	}
	void LockAccount(int32_t ID, NamedLock& rNamedLock);

private:
	static std::string MakeAccountRootDir(int32_t ID, int DiscSet);

	BackupStoreAccountDatabase &mrDatabase;
};

// max size of soft limit as percent of hard limit
#define MAX_SOFT_LIMIT_SIZE		97

#endif // BACKUPSTOREACCOUNTS__H

