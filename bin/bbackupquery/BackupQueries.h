// --------------------------------------------------------------------------
//
// File
//		Name:    BackupQueries.h
//		Purpose: Perform various queries on the backup store server.
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------

#ifndef BACKUPQUERIES__H
#define BACKUPQUERIES__H

#include <vector>
#include <string>

#include "BoxTime.h"
#include "BoxBackupCompareParams.h"

class BackupProtocolClient;
class Configuration;
class ExcludeList;

typedef struct
{
	const char* name;
	const char* opts;
}
QueryCommandSpecification;

// Data about commands
extern QueryCommandSpecification commands[];

typedef enum
{
	Command_Quit = 0,
	Command_Exit,
	Command_List,
	Command_pwd,
	Command_cd,
	Command_lcd,
	Command_sh,
	Command_GetObject,
	Command_Get,
	Command_Compare,
	Command_Restore,
	Command_Help,
	Command_Usage,
	Command_Undelete,
	Command_Delete,
}
CommandType;

extern const char *alias[];
extern const int aliasIs[];

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupQueries
//		Purpose: Perform various queries on the backup store server.
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
class BackupQueries
{
public:
	BackupQueries(BackupProtocolClient &rConnection,
		const Configuration &rConfiguration,
		bool readWrite);
	~BackupQueries();
private:
	BackupQueries(const BackupQueries &);
public:

	void DoCommand(const char *Command, bool isFromCommandLine);

	// Ready to stop?
	bool Stop() {return mQuitNow;}
	
	// Return code?
	int GetReturnCode() {return mReturnCode;}

private:
	// Commands
	void CommandList(const std::vector<std::string> &args, const bool *opts);
	void CommandChangeDir(const std::vector<std::string> &args, const bool *opts);
	void CommandChangeLocalDir(const std::vector<std::string> &args);
	void CommandGetObject(const std::vector<std::string> &args, const bool *opts);
	void CommandGet(std::vector<std::string> args, const bool *opts);
	void CommandCompare(const std::vector<std::string> &args, const bool *opts);
	void CommandRestore(const std::vector<std::string> &args, const bool *opts);
	void CommandUndelete(const std::vector<std::string> &args, const bool *opts);
	void CommandDelete(const std::vector<std::string> &args,
		const bool *opts);
	void CommandUsage(const bool *opts);
	void CommandUsageDisplayEntry(const char *Name, int64_t Size,
		int64_t HardLimit, int32_t BlockSize, bool MachineReadable);
	void CommandHelp(const std::vector<std::string> &args);

	// Implementations
	void List(int64_t DirID, const std::string &rListRoot, const bool *opts,
		bool FirstLevel);
	
public:
	class CompareParams : public BoxBackupCompareParams
	{
	public:
		CompareParams(bool QuickCompare, bool IgnoreExcludes,
			bool IgnoreAttributes, box_time_t LatestFileUploadTime);
		
		bool mQuietCompare;
		int mDifferences;
		int mDifferencesExplainedByModTime;
		int mUncheckedFiles;
		int mExcludedDirs;
		int mExcludedFiles;

		std::string ConvertForConsole(const std::string& rUtf8String)
		{
		#ifdef WIN32
			std::string output;
			
			if(!ConvertUtf8ToConsole(rUtf8String.c_str(), output))
			{
				BOX_WARNING("Character set conversion failed "
					"on string: " << rUtf8String);
				return rUtf8String;
			}
			
			return output;
		#else
			return rUtf8String;
		#endif
		}

		virtual void NotifyLocalDirMissing(const std::string& rLocalPath,
			const std::string& rRemotePath)
		{
			BOX_WARNING("Local directory '" <<
				ConvertForConsole(rLocalPath) << "' "
				"does not exist, but remote directory does.");
			mDifferences ++;
		}
		
		virtual void NotifyLocalDirAccessFailed(
			const std::string& rLocalPath,
			const std::string& rRemotePath)
		{
			BOX_LOG_SYS_WARNING("Failed to access local directory "
				"'" << ConvertForConsole(rLocalPath) << "'");
			mUncheckedFiles ++;
		}

		virtual void NotifyStoreDirMissingAttributes(
			const std::string& rLocalPath,
			const std::string& rRemotePath)
		{
			BOX_WARNING("Store directory '" <<
				ConvertForConsole(rRemotePath) << "' "
				"doesn't have attributes.");
		}

		virtual void NotifyRemoteFileMissing(
			const std::string& rLocalPath,
			const std::string& rRemotePath,
			bool modifiedAfterLastSync)
		{
			BOX_WARNING("Local file '" <<
				ConvertForConsole(rLocalPath) << "' " 
				"exists, but remote file '" <<
				ConvertForConsole(rRemotePath) << "' "
				"does not.");
			mDifferences ++;
			
			if(modifiedAfterLastSync)
			{
				mDifferencesExplainedByModTime ++;
				BOX_INFO("(the file above was modified after "
					"the last sync time -- might be "
					"reason for difference)");
			}
		}

		virtual void NotifyLocalFileMissing(
			const std::string& rLocalPath,
			const std::string& rRemotePath)
		{
			BOX_WARNING("Remote file '" <<
				ConvertForConsole(rRemotePath) << "' " 
				"exists, but local file '" <<
				ConvertForConsole(rLocalPath) << "' does not.");
			mDifferences ++;
		}

		virtual void NotifyExcludedFileNotDeleted(
			const std::string& rLocalPath,
			const std::string& rRemotePath)
		{
			BOX_WARNING("Local file '" <<
				ConvertForConsole(rLocalPath) << "' " 
				"is excluded, but remote file '" <<
				ConvertForConsole(rRemotePath) << "' "
				"still exists.");
			mDifferences ++;
		}
		
		virtual void NotifyDownloadFailed(const std::string& rLocalPath,
			const std::string& rRemotePath, int64_t NumBytes,
			BoxException& rException)
		{
			BOX_ERROR("Failed to download remote file '" <<
				ConvertForConsole(rRemotePath) << "': " <<
				rException.what() << " (" <<
				rException.GetType() << "/" <<
				rException.GetSubType() << ")");
			mUncheckedFiles ++;
		}

		virtual void NotifyDownloadFailed(const std::string& rLocalPath,
			const std::string& rRemotePath, int64_t NumBytes,
			std::exception& rException)
		{
			BOX_ERROR("Failed to download remote file '" <<
				ConvertForConsole(rRemotePath) << "': " <<
				rException.what());
			mUncheckedFiles ++;
		}

		virtual void NotifyDownloadFailed(const std::string& rLocalPath,
			const std::string& rRemotePath, int64_t NumBytes)
		{
			BOX_ERROR("Failed to download remote file '" <<
				ConvertForConsole(rRemotePath));
			mUncheckedFiles ++;
		}

		virtual void NotifyExcludedFile(const std::string& rLocalPath,
			const std::string& rRemotePath)
		{
			mExcludedFiles ++;
		}

		virtual void NotifyExcludedDir(const std::string& rLocalPath,
			const std::string& rRemotePath)
		{
			mExcludedDirs ++;
		}

		virtual void NotifyDirComparing(const std::string& rLocalPath,
			const std::string& rRemotePath)
		{
		}

		virtual void NotifyDirCompared(
			const std::string& rLocalPath,
			const std::string& rRemotePath,
			bool HasDifferentAttributes,
			bool modifiedAfterLastSync)
		{
			if(HasDifferentAttributes)
			{
				BOX_WARNING("Local directory '" <<
					ConvertForConsole(rLocalPath) << "' "
					"has different attributes to "
					"store directory '" <<
					ConvertForConsole(rRemotePath) << "'.");
				mDifferences ++;
				
				if(modifiedAfterLastSync)
				{
					mDifferencesExplainedByModTime ++;
					BOX_INFO("(the directory above was "
						"modified after the last sync "
						"time -- might be reason for "
						"difference)");
				}
			}
		}

		virtual void NotifyFileComparing(const std::string& rLocalPath,
			const std::string& rRemotePath)
		{
		}
		
		virtual void NotifyFileCompared(const std::string& rLocalPath,
			const std::string& rRemotePath, int64_t NumBytes,
			bool HasDifferentAttributes, bool HasDifferentContents,
			bool ModifiedAfterLastSync, bool NewAttributesApplied)
		{
			int NewDifferences = 0;
			
			if(HasDifferentAttributes)
			{
				BOX_WARNING("Local file '" <<
					ConvertForConsole(rLocalPath) << "' "
					"has different attributes to "
					"store file '" <<
					ConvertForConsole(rRemotePath) << "'.");
				NewDifferences ++;
			}

			if(HasDifferentContents)
			{
				BOX_WARNING("Local file '" <<
					ConvertForConsole(rLocalPath) << "' "
					"has different contents to "
					"store file '" <<
					ConvertForConsole(rRemotePath) << "'.");
				NewDifferences ++;
			}
			
			if(HasDifferentAttributes || HasDifferentContents)
			{
				if(ModifiedAfterLastSync)
				{
					mDifferencesExplainedByModTime +=
						NewDifferences;
					BOX_INFO("(the file above was modified "
						"after the last sync time -- "
						"might be reason for difference)");
				}
				else if(NewAttributesApplied)
				{
					BOX_INFO("(the file above has had new "
						"attributes applied)\n");
				}
			}
			
			mDifferences += NewDifferences;
		}
	};
	void CompareLocation(const std::string &rLocation,
		BoxBackupCompareParams &rParams);
	void Compare(const std::string &rStoreDir,
		const std::string &rLocalDir, BoxBackupCompareParams &rParams);
	void Compare(int64_t DirID, const std::string &rStoreDir,
		const std::string &rLocalDir, BoxBackupCompareParams &rParams);

public:

	class ReturnCode
	{
		public:
		enum {
			Command_OK = 0,
			Compare_Same = 1,
			Compare_Different,
			Compare_Error,
			Command_Error,
		} Type;
	};

private:

	// Utility functions
	int64_t FindDirectoryObjectID(const std::string &rDirName,
		bool AllowOldVersion = false, bool AllowDeletedDirs = false,
		std::vector<std::pair<std::string, int64_t> > *pStack = 0);
	int64_t FindFileID(const std::string& rNameOrIdString,
		const bool *opts, int64_t *pDirIdOut,
		std::string* pFileNameOut, int16_t flagsInclude,
		int16_t flagsExclude, int16_t* pFlagsOut);
	int64_t GetCurrentDirectoryID();
	std::string GetCurrentDirectoryName();
	void SetReturnCode(int code) {mReturnCode = code;}

private:
	bool mReadWrite;
	BackupProtocolClient &mrConnection;
	const Configuration &mrConfiguration;
	bool mQuitNow;
	std::vector<std::pair<std::string, int64_t> > mDirStack;
	bool mRunningAsRoot;
	bool mWarnedAboutOwnerAttributes;
	int mReturnCode;
};

#endif // BACKUPQUERIES__H

