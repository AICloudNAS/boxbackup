// --------------------------------------------------------------------------
//
// File
//		Name:    BackupDaemon.h
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#ifndef BACKUPDAEMON__H
#define BACKUPDAEMON__H

#include <vector>
#include <string>
#include <memory>

#include "BackupClientContext.h"
#include "BackupClientDirectoryRecord.h"
#include "BoxTime.h"
#include "Daemon.h"
#include "Logging.h"
#include "Socket.h"
#include "SocketListen.h"
#include "SocketStream.h"
#include "TLSContext.h"

#include "autogen_BackupProtocolClient.h"

#ifdef WIN32
	#include "WinNamedPipeListener.h"
	#include "WinNamedPipeStream.h"
#endif

#ifdef ENABLE_VSS
#	include <comdef.h>
#	include <Vss.h>
#	include <VsWriter.h>
#	include <VsBackup.h>
#endif

class BackupClientDirectoryRecord;
class BackupClientContext;
class Configuration;
class BackupClientInodeToIDMap;
class ExcludeList;
class IOStreamGetLine;
class Archive;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupDaemon
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
class BackupDaemon : public Daemon, ProgressNotifier, LocationResolver,
RunStatusProvider, SysadminNotifier
{
public:
	BackupDaemon();
	~BackupDaemon();

private:
	// methods below do partial (specialized) serialization of 
	// client state only
	bool SerializeStoreObjectInfo(box_time_t theLastSyncTime,
		box_time_t theNextSyncTime) const;
	bool DeserializeStoreObjectInfo(box_time_t & theLastSyncTime,
		box_time_t & theNextSyncTime);
	bool DeleteStoreObjectInfo() const;
	BackupDaemon(const BackupDaemon &);

public:
	#ifdef WIN32
		// add command-line options to handle Windows services
		std::string GetOptionString();
		int ProcessOption(signed int option);
		int Main(const std::string &rConfigFileName);

		// This shouldn't be here, but apparently gcc on
		// Windows has no idea about inherited methods...
		virtual int Main(const char *DefaultConfigFile, int argc,
			const char *argv[])
		{
			return Daemon::Main(DefaultConfigFile, argc, argv);
		}
	#endif

	void Run();
	virtual const char *DaemonName() const;
	virtual std::string DaemonBanner() const;
	virtual void Usage();
	const ConfigurationVerify *GetConfigVerify() const;

	bool FindLocationPathName(const std::string &rLocationName, std::string &rPathOut) const;

	enum
	{
		// Add stuff to this, make sure the textual equivalents in SetState() are changed too.
		State_Initialising = -1,
		State_Idle = 0,
		State_Connected = 1,
		State_Error = 2,
		State_StorageLimitExceeded = 3
	};

	int GetState() {return mState;}

	// Allow other classes to call this too
	void NotifySysadmin(SysadminNotifier::EventCode Event);

private:
	void Run2();

public:
	void InitCrypto();
	void RunSyncNowWithExceptionHandling();
	void RunSyncNow();
	void OnBackupStart();
	void OnBackupFinish();
	// TouchFileInWorkingDir is only here for use by Boxi.
	// This does NOT constitute an API!
	void TouchFileInWorkingDir(const char *Filename);

private:
	void DeleteAllLocations();
	void SetupLocations(BackupClientContext &rClientContext, const Configuration &rLocationsConf);

	void DeleteIDMapVector(std::vector<BackupClientInodeToIDMap *> &rVector);
	void DeleteAllIDMaps()
	{
		DeleteIDMapVector(mCurrentIDMaps);
		DeleteIDMapVector(mNewIDMaps);
	}
	void FillIDMapVector(std::vector<BackupClientInodeToIDMap *> &rVector, bool NewMaps);
	
	void SetupIDMapsForSync();
	void CommitIDMapsAfterSync();
	void DeleteCorruptBerkelyDbFiles();
	
	void MakeMapBaseName(unsigned int MountNumber, std::string &rNameOut) const;

	void SetState(int State);
	
	void WaitOnCommandSocket(box_time_t RequiredDelay, bool &DoSyncFlagOut, bool &SyncIsForcedOut);
	void CloseCommandConnection();
	void SendSyncStartOrFinish(bool SendStart);
	
	void DeleteUnusedRootDirEntries(BackupClientContext &rContext);

#ifdef PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
	// For warning user about potential security hole
	virtual void SetupInInitialProcess();
#endif

	int UseScriptToSeeIfSyncAllowed();

public:
	class Location
	{
	public:
		Location();
		~Location();

		void Deserialize(Archive & rArchive);
		void Serialize(Archive & rArchive) const;
	private:
		Location(const Location &);	// copy not allowed
		Location &operator=(const Location &);
	public:
		std::string mName;
		std::string mPath;
		std::auto_ptr<BackupClientDirectoryRecord> mpDirectoryRecord;
		int mIDMapIndex;
		ExcludeList *mpExcludeFiles;
		ExcludeList *mpExcludeDirs;

#ifdef ENABLE_VSS
		bool mIsSnapshotCreated;
		VSS_ID mSnapshotVolumeId;
		std::string mSnapshotPath;
#endif
	};
	
	typedef const std::vector<Location *> Locations;
	Locations GetLocations() { return mLocations; }
	
private:
	int mState;		// what the daemon is currently doing

	std::vector<Location *> mLocations;
	
	std::vector<std::string> mIDMapMounts;
	std::vector<BackupClientInodeToIDMap *> mCurrentIDMaps;
	std::vector<BackupClientInodeToIDMap *> mNewIDMaps;
	
	int mDeleteRedundantLocationsAfter;

	// For the command socket
	class CommandSocketInfo
	{
	public:
		CommandSocketInfo();
		~CommandSocketInfo();
	private:
		CommandSocketInfo(const CommandSocketInfo &);	// no copying
		CommandSocketInfo &operator=(const CommandSocketInfo &);
	public:
#ifdef WIN32
		WinNamedPipeListener<1 /* listen backlog */> mListeningSocket;
		std::auto_ptr<WinNamedPipeStream> mpConnectedSocket;
#else
		SocketListen<SocketStream, 1 /* listen backlog */> mListeningSocket;
		std::auto_ptr<SocketStream> mpConnectedSocket;
#endif
		IOStreamGetLine *mpGetLine;
	};
	
	// Using a socket?
	std::auto_ptr<CommandSocketInfo> mapCommandSocketInfo;
	
	// Stop notifications being repeated.
	SysadminNotifier::EventCode mLastNotifiedEvent;

	// Unused entries in the root directory wait a while before being deleted
	box_time_t mDeleteUnusedRootDirEntriesAfter;	// time to delete them
	std::vector<std::pair<int64_t,std::string> > mUnusedRootDirEntries;

	int64_t mClientStoreMarker;
	bool mStorageLimitExceeded;
	bool mReadErrorsOnFilesystemObjects;
	box_time_t mLastSyncTime, mNextSyncTime;
	box_time_t mCurrentSyncStartTime, mUpdateStoreInterval;
	TLSContext mTlsContext;
	bool mDeleteStoreObjectInfoFile;
	bool mDoSyncForcedByPreviousSyncError;

public:
 	bool StopRun() { return this->Daemon::StopRun(); }
	bool StorageLimitExceeded() { return mStorageLimitExceeded; }
 
private:
	bool mLogAllFileAccess;

public:
	ProgressNotifier*  GetProgressNotifier()  { return mpProgressNotifier; }
	LocationResolver*  GetLocationResolver()  { return mpLocationResolver; }
	RunStatusProvider* GetRunStatusProvider() { return mpRunStatusProvider; }
	SysadminNotifier*  GetSysadminNotifier()  { return mpSysadminNotifier; }
	void SetProgressNotifier (ProgressNotifier*  p) { mpProgressNotifier = p; }
	void SetLocationResolver (LocationResolver*  p) { mpLocationResolver = p; }
	void SetRunStatusProvider(RunStatusProvider* p) { mpRunStatusProvider = p; }
	void SetSysadminNotifier (SysadminNotifier*  p) { mpSysadminNotifier = p; }
		
private:
	ProgressNotifier* mpProgressNotifier;
	LocationResolver* mpLocationResolver;
	RunStatusProvider* mpRunStatusProvider;
	SysadminNotifier* mpSysadminNotifier;
	
 	/* ProgressNotifier implementation */
public:
	virtual void NotifyIDMapsSetup(BackupClientContext& rContext) { }

 	virtual void NotifyScanDirectory(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath) 
	{
		if (mLogAllFileAccess)
		{
			BOX_INFO("Scanning directory: " << rLocalPath);
		} 
	}
 	virtual void NotifyDirStatFailed(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath, 
 		const std::string& rErrorMsg)
 	{
		BOX_WARNING("Failed to access directory: " << rLocalPath
			<< ": " << rErrorMsg);
 	}
 	virtual void NotifyFileStatFailed(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath,
 		const std::string& rErrorMsg)
 	{
		BOX_WARNING("Failed to access file: " << rLocalPath
			<< ": " << rErrorMsg);
 	}
 	virtual void NotifyDirListFailed(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath,
 		const std::string& rErrorMsg)
 	{
		BOX_WARNING("Failed to list directory: " << rLocalPath
			<< ": " << rErrorMsg);
 	}
	virtual void NotifyMountPointSkipped(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		#ifdef WIN32
			BOX_WARNING("Ignored directory: " << rLocalPath << 
				": is an NTFS junction/reparse point; create "
				"a new location if you want to back it up");
		#else
			BOX_WARNING("Ignored directory: " << rLocalPath << 
				": is a mount point; create a new location "
				"if you want to back it up");
		#endif
	}
	virtual void NotifyFileExcluded(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		if (mLogAllFileAccess)
		{
			BOX_INFO("Skipping excluded file: " << rLocalPath);
		} 
	}
	virtual void NotifyDirExcluded(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		if (mLogAllFileAccess)
		{
			BOX_INFO("Skipping excluded directory: " << rLocalPath);
		} 
	}
	virtual void NotifyUnsupportedFileType(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		BOX_WARNING("Ignoring file of unknown type: " << rLocalPath);
	}
 	virtual void NotifyFileReadFailed(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath,
 		const std::string& rErrorMsg)
 	{
		BOX_WARNING("Error reading file: " << rLocalPath
			<< ": " << rErrorMsg);
 	}
 	virtual void NotifyFileModifiedInFuture(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath)
 	{
		BOX_WARNING("Some files have modification times excessively "
			"in the future. Check clock synchronisation. "
			"Example file (only one shown): " << rLocalPath);
 	}
 	virtual void NotifyFileSkippedServerFull(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath) 
	{
		BOX_WARNING("Skipped file: server is full: " << rLocalPath);
	}
 	virtual void NotifyFileUploadException(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath,
 		const BoxException& rException)
 	{
		if (rException.GetType() == CommonException::ExceptionType &&
			rException.GetSubType() == CommonException::AccessDenied)
		{
			BOX_ERROR("Failed to upload file: " << rLocalPath 
				<< ": Access denied");
		}
		else
		{
			BOX_ERROR("Failed to upload file: " << rLocalPath 
				<< ": caught exception: " << rException.what() 
				<< " (" << rException.GetType()
				<< "/"  << rException.GetSubType() << ")");
		}
 	}
  	virtual void NotifyFileUploadServerError(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath,
 		int type, int subtype)
 	{
		std::ostringstream msgs;
		if (type != BackupProtocolClientError::ErrorType)
		{
			msgs << "unknown error type " << type;
		}
		else
		{
			switch(subtype)
			{
			case BackupProtocolClientError::Err_WrongVersion:
				msgs << "WrongVersion";
				break;
			case BackupProtocolClientError::Err_NotInRightProtocolPhase:
				msgs << "NotInRightProtocolPhase";
				break;
			case BackupProtocolClientError::Err_BadLogin:
				msgs << "BadLogin";
				break;
			case BackupProtocolClientError::Err_CannotLockStoreForWriting:
				msgs << "CannotLockStoreForWriting";
				break;
			case BackupProtocolClientError::Err_SessionReadOnly:
				msgs << "SessionReadOnly";
				break;
			case BackupProtocolClientError::Err_FileDoesNotVerify:
				msgs << "FileDoesNotVerify";
				break;
			case BackupProtocolClientError::Err_DoesNotExist:
				msgs << "DoesNotExist";
				break;
			case BackupProtocolClientError::Err_DirectoryAlreadyExists:
				msgs << "DirectoryAlreadyExists";
				break;
			case BackupProtocolClientError::Err_CannotDeleteRoot:
				msgs << "CannotDeleteRoot";
				break;
			case BackupProtocolClientError::Err_TargetNameExists:
				msgs << "TargetNameExists";
				break;
			case BackupProtocolClientError::Err_StorageLimitExceeded:
				msgs << "StorageLimitExceeded";
				break;
			case BackupProtocolClientError::Err_DiffFromFileDoesNotExist:
				msgs << "DiffFromFileDoesNotExist";
				break;
			case BackupProtocolClientError::Err_DoesNotExistInDirectory:
				msgs << "DoesNotExistInDirectory";
				break;
			case BackupProtocolClientError::Err_PatchConsistencyError:
				msgs << "PatchConsistencyError";
				break;
			default:
				msgs << "unknown error subtype " << subtype;
			}
		}

		BOX_ERROR("Failed to upload file: " << rLocalPath 
			<< ": server error: " << msgs.str());
 	}
 	virtual void NotifyFileUploading(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath) 
	{ 
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Uploading complete file: " << rLocalPath);
		} 
	}
 	virtual void NotifyFileUploadingPatch(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath) 
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Uploading patch to file: " << rLocalPath);
		} 
	}
 	virtual void NotifyFileUploadingAttributes(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath) 
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Uploading new file attributes: " << 
				rLocalPath);
		} 
	}
 	virtual void NotifyFileUploaded(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath,
 		int64_t FileSize, int64_t UploadedSize) 
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Uploaded file: " << rLocalPath << ", "
				"total size = " << FileSize << ", "
				"uploaded size = " << UploadedSize);
		} 
	}
 	virtual void NotifyFileSynchronised(
 		const BackupClientDirectoryRecord* pDirRecord,
 		const std::string& rLocalPath,
 		int64_t FileSize) 
	{
		if (mLogAllFileAccess)
		{
			BOX_INFO("Synchronised file: " << rLocalPath);
		} 
	}
	virtual void NotifyDirectoryDeleted(
		int64_t ObjectID,
		const std::string& rRemotePath)
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Deleted directory: " << rRemotePath << 
				" (ID " << BOX_FORMAT_OBJECTID(ObjectID) <<
				")");
		}
	}
	virtual void NotifyFileDeleted(
		int64_t ObjectID,
		const std::string& rRemotePath)
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Deleted file: " << rRemotePath << 
				" (ID " << BOX_FORMAT_OBJECTID(ObjectID) <<
				")");
		}
	}
	virtual void NotifyReadProgress(int64_t readSize, int64_t offset,
		int64_t length, box_time_t elapsed, box_time_t finish)
	{
		BOX_TRACE("Read " << readSize << " bytes at " << offset << 
			", " << (length - offset) << " remain, eta " <<
			BoxTimeToSeconds(finish - elapsed) << "s");
	}
	virtual void NotifyReadProgress(int64_t readSize, int64_t offset,
		int64_t length)
	{
		BOX_TRACE("Read " << readSize << " bytes at " << offset << 
			", " << (length - offset) << " remain");
	}
	virtual void NotifyReadProgress(int64_t readSize, int64_t offset)
	{
		BOX_TRACE("Read " << readSize << " bytes at " << offset << 
			", unknown bytes remaining");
	}

#ifdef WIN32
	private:
	bool mInstallService, mRemoveService, mRunAsService;
	std::string mServiceName;
#endif

#ifdef ENABLE_VSS
	IVssBackupComponents* mpVssBackupComponents;
	void CreateVssBackupComponents();
	bool WaitForAsync(IVssAsync *pAsync, const std::string& description);
	typedef HRESULT (__stdcall IVssBackupComponents::*AsyncMethod)(IVssAsync**);
	bool CallAndWaitForAsync(AsyncMethod method,
		const std::string& description);
	void CleanupVssBackupComponents();
#endif
};

#endif // BACKUPDAEMON__H
