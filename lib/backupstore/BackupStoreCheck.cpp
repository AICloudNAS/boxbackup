// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreCheck.cpp
//		Purpose: Check a store for consistency
//		Created: 21/4/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif

#include "BackupStoreCheck.h"
#include "StoreStructure.h"
#include "RaidFileRead.h"
#include "RaidFileWrite.h"
#include "autogen_BackupStoreException.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreFile.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreConstants.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::BackupStoreCheck(const std::string &, int, int32_t, bool, bool)
//		Purpose: Constructor
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
BackupStoreCheck::BackupStoreCheck(const std::string &rStoreRoot, int DiscSetNumber, int32_t AccountID, bool FixErrors, bool Quiet)
	: mStoreRoot(rStoreRoot),
	  mDiscSetNumber(DiscSetNumber),
	  mAccountID(AccountID),
	  mFixErrors(FixErrors),
	  mQuiet(Quiet),
	  mNumberErrorsFound(0),
	  mLastIDInInfo(0),
	  mpInfoLastBlock(0),
	  mInfoLastBlockEntries(0),
	  mLostDirNameSerial(0),
	  mLostAndFoundDirectoryID(0),
	  mBlocksUsed(0),
	  mBlocksInCurrentFiles(0),
	  mBlocksInOldFiles(0),
	  mBlocksInDeletedFiles(0),
	  mBlocksInDirectories(0),
	  mNumFiles(0),
	  mNumOldFiles(0),
	  mNumDeletedFiles(0),
	  mNumDirectories(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::~BackupStoreCheck()
//		Purpose: Destructor
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
BackupStoreCheck::~BackupStoreCheck()
{
	// Clean up
	FreeInfo();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::Check()
//		Purpose: Perform the check on the given account. You need to
//			 hold a lock on the account before calling this!
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::Check()
{
	std::string writeLockFilename;
	StoreStructure::MakeWriteLockFilename(mStoreRoot, mDiscSetNumber, writeLockFilename);
	ASSERT(FileExists(writeLockFilename));

	if(!mQuiet && mFixErrors)
	{
		BOX_NOTICE("Will fix errors encountered during checking.");
	}

	// Phase 1, check objects
	if(!mQuiet)
	{
		BOX_INFO("Checking store account ID " <<
			BOX_FORMAT_ACCOUNT(mAccountID) << "...");
		BOX_INFO("Phase 1, check objects...");
	}
	CheckObjects();
	
	// Phase 2, check directories
	if(!mQuiet)
	{
		BOX_INFO("Phase 2, check directories...");
	}
	CheckDirectories();
	
	// Phase 3, check root
	if(!mQuiet)
	{
		BOX_INFO("Phase 3, check root...");
	}
	CheckRoot();

	// Phase 4, check unattached objects
	if(!mQuiet)
	{
		BOX_INFO("Phase 4, fix unattached objects...");
	}
	CheckUnattachedObjects();

	// Phase 5, fix bad info
	if(!mQuiet)
	{
		BOX_INFO("Phase 5, fix unrecovered inconsistencies...");
	}
	FixDirsWithWrongContainerID();
	FixDirsWithLostDirs();
	
	// Phase 6, regenerate store info
	if(!mQuiet)
	{
		BOX_INFO("Phase 6, regenerate store info...");
	}
	WriteNewStoreInfo();
	
//	DUMP_OBJECT_INFO
	
	if(mNumberErrorsFound > 0)
	{
		BOX_WARNING("Finished checking store account ID " <<
			BOX_FORMAT_ACCOUNT(mAccountID) << ": " <<
			mNumberErrorsFound << " errors found");
		if(!mFixErrors)
		{
			BOX_WARNING("No changes to the store account "
				"have been made.");
		}
		if(!mFixErrors && mNumberErrorsFound > 0)
		{
			BOX_WARNING("Run again with fix option to "
				"fix these errors");
		}
		if(mFixErrors && mNumberErrorsFound > 0)
		{
			BOX_WARNING("You should now use bbackupquery "
				"on the client machine to examine the store.");
			if(mLostAndFoundDirectoryID != 0)
			{
				BOX_WARNING("A lost+found directory was "
					"created in the account root.\n"
					"This contains files and directories "
					"which could not be matched to "
					"existing directories.\n"\
					"bbackupd will delete this directory "
					"in a few days time.");
			}
		}
	}
	else
	{
		BOX_NOTICE("Finished checking store account ID " <<
			BOX_FORMAT_ACCOUNT(mAccountID) << ": "
			"no errors found");
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    static TwoDigitHexToInt(const char *, int &)
//		Purpose: Convert a two digit hex string to an int, returning whether it's valid or not
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
static inline bool TwoDigitHexToInt(const char *String, int &rNumberOut)
{
	int n = 0;
	// Char 0
	if(String[0] >= '0' && String[0] <= '9')
	{
		n = (String[0] - '0') << 4;
	}
	else if(String[0] >= 'a' && String[0] <= 'f')
	{
		n = ((String[0] - 'a') + 0xa) << 4;
	}
	else
	{
		return false;
	}
	// Char 1
	if(String[1] >= '0' && String[1] <= '9')
	{
		n |= String[1] - '0';
	}
	else if(String[1] >= 'a' && String[1] <= 'f')
	{
		n |= (String[1] - 'a') + 0xa;
	}
	else
	{
		return false;
	}

	// Return a valid number
	rNumberOut = n;
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckObjects()
//		Purpose: Read in the contents of the directory, recurse to other levels,
//				 checking objects for sanity and readability
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CheckObjects()
{
	// Maximum start ID of directories -- worked out by looking at disc contents, not trusting anything
	int64_t maxDir = 0;

	// Find the maximum directory starting ID
	{
		// Make sure the starting root dir doesn't end with '/'.
		std::string start(mStoreRoot);
		if(start.size() > 0 && start[start.size() - 1] == '/')
		{
			start.resize(start.size() - 1);
		}
	
		maxDir = CheckObjectsScanDir(0, 1, mStoreRoot);
		BOX_TRACE("Max dir starting ID is " <<
			BOX_FORMAT_OBJECTID(maxDir));
	}
	
	// Then go through and scan all the objects within those directories
	for(int64_t d = 0; d <= maxDir; d += (1<<STORE_ID_SEGMENT_LENGTH))
	{
		CheckObjectsDir(d);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckObjectsScanDir(int64_t, int, int, const std::string &)
//		Purpose: Read in the contents of the directory, recurse to other levels,
//				 return the maximum starting ID of any directory found.
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
int64_t BackupStoreCheck::CheckObjectsScanDir(int64_t StartID, int Level, const std::string &rDirName)
{
	//TRACE2("Scan directory for max dir starting ID %s, StartID %lld\n", rDirName.c_str(), StartID);

	int64_t maxID = StartID;

	// Read in all the directories, and recurse downwards
	{
		std::vector<std::string> dirs;
		RaidFileRead::ReadDirectoryContents(mDiscSetNumber, rDirName,
			RaidFileRead::DirReadType_DirsOnly, dirs);

		for(std::vector<std::string>::const_iterator i(dirs.begin()); i != dirs.end(); ++i)
		{
			// Check to see if it's the right name
			int n = 0;
			if((*i).size() == 2 && TwoDigitHexToInt((*i).c_str(), n)
				&& n < (1<<STORE_ID_SEGMENT_LENGTH))
			{
				// Next level down
				int64_t mi = CheckObjectsScanDir(StartID | (n << (Level * STORE_ID_SEGMENT_LENGTH)), Level + 1,
					rDirName + DIRECTORY_SEPARATOR + *i);
				// Found a greater starting ID?
				if(mi > maxID)
				{
					maxID = mi;
				}
			}
			else
			{
				BOX_WARNING("Spurious or invalid directory " <<
					rDirName << DIRECTORY_SEPARATOR << 
					(*i) << " found, " <<
					(mFixErrors?"deleting":"delete manually"));
				++mNumberErrorsFound;
			}
		}
	}

	return maxID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckObjectsDir(int64_t)
//		Purpose: Check all the files within this directory which has
//			 the given starting ID.
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CheckObjectsDir(int64_t StartID)
{
	// Make directory name -- first generate the filename of an entry in it
	std::string dirName;
	StoreStructure::MakeObjectFilename(StartID, mStoreRoot, mDiscSetNumber, dirName, false /* don't make sure the dir exists */);
	// Check expectations
	ASSERT(dirName.size() > 4 && 
		dirName[dirName.size() - 4] == DIRECTORY_SEPARATOR_ASCHAR);
	// Remove the filename from it
	dirName.resize(dirName.size() - 4); // four chars for "/o00"
	
	// Check directory exists
	if(!RaidFileRead::DirectoryExists(mDiscSetNumber, dirName))
	{
		BOX_WARNING("RaidFile dir " << dirName << " does not exist");
		return;
	}

	// Read directory contents
	std::vector<std::string> files;
	RaidFileRead::ReadDirectoryContents(mDiscSetNumber, dirName,
		RaidFileRead::DirReadType_FilesOnly, files);
	
	// Array of things present
	bool idsPresent[(1<<STORE_ID_SEGMENT_LENGTH)];
	for(int l = 0; l < (1<<STORE_ID_SEGMENT_LENGTH); ++l)
	{
		idsPresent[l] = false;
	}
	
	// Parse each entry, building up a list of object IDs which are present in the dir.
	// This is done so that whatever order is retured from the directory, objects are scanned
	// in order.
	// Filename must begin with a 'o' and be three characters long, otherwise it gets deleted.
	for(std::vector<std::string>::const_iterator i(files.begin()); i != files.end(); ++i)
	{
		bool fileOK = true;
		int n = 0;
		if((*i).size() == 3 && (*i)[0] == 'o' && TwoDigitHexToInt((*i).c_str() + 1, n)
			&& n < (1<<STORE_ID_SEGMENT_LENGTH))
		{
			// Filename is valid, mark as existing
			idsPresent[n] = true;
		}
		// No other files should be present in subdirectories
		else if(StartID != 0)
		{
			fileOK = false;
		}
		// info and refcount databases are OK in the root directory
		else if(*i == "info" || *i == "refcount.db")
		{
			fileOK = true;
		}
		else
		{
			fileOK = false;
		}
		
		if(!fileOK)
		{
			// Unexpected or bad file, delete it
			BOX_WARNING("Spurious file " << dirName << 
				DIRECTORY_SEPARATOR << (*i) << " found" <<
				(mFixErrors?", deleting":""));
			++mNumberErrorsFound;
			if(mFixErrors)
			{
				RaidFileWrite del(mDiscSetNumber, dirName + DIRECTORY_SEPARATOR + *i);
				del.Delete();
			}
		}
	}
	
	// Check all the objects found in this directory
	for(int i = 0; i < (1<<STORE_ID_SEGMENT_LENGTH); ++i)
	{
		if(idsPresent[i])
		{
			// Check the object is OK, and add entry
			char leaf[8];
			::sprintf(leaf, DIRECTORY_SEPARATOR "o%02x", i);
			if(!CheckAndAddObject(StartID | i, dirName + leaf))
			{
				// File was bad, delete it
				BOX_WARNING("Corrupted file " << dirName <<
					leaf << " found" <<
					(mFixErrors?", deleting":""));
				++mNumberErrorsFound;
				if(mFixErrors)
				{
					RaidFileWrite del(mDiscSetNumber, dirName + leaf);
					del.Delete();
				}
			}
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckAndAddObject(int64_t,
//			 const std::string &)
//		Purpose: Check a specific object and add it to the list
//			 if it's OK. If there are any errors with the
//			 reading, return false and it'll be deleted.
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
bool BackupStoreCheck::CheckAndAddObject(int64_t ObjectID,
	const std::string &rFilename)
{
	// Info on object...
	bool isFile = true;
	int64_t containerID = -1;
	int64_t size = -1;

	try
	{
		// Open file
		std::auto_ptr<RaidFileRead> file(
			RaidFileRead::Open(mDiscSetNumber, rFilename));
		size = file->GetDiscUsageInBlocks();
		
		// Read in first four bytes -- don't have to worry about
		// retrying if not all bytes read as is RaidFile
		uint32_t signature;
		if(file->Read(&signature, sizeof(signature)) != sizeof(signature))
		{
			// Too short, can't read signature from it
			return false;
		}
		// Seek back to beginning
		file->Seek(0, IOStream::SeekType_Absolute);
		
		// Then... check depending on the type
		switch(ntohl(signature))
		{
		case OBJECTMAGIC_FILE_MAGIC_VALUE_V1:
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		case OBJECTMAGIC_FILE_MAGIC_VALUE_V0:
#endif
			// File... check
			containerID = CheckFile(ObjectID, *file);
			break;

		case OBJECTMAGIC_DIR_MAGIC_VALUE:
			isFile = false;
			containerID = CheckDirInitial(ObjectID, *file);
			break;

		default:
			// Unknown signature. Bad file. Very bad file.
			return false;
			break;
		}
		
		// Add to usage counts
		mBlocksUsed += size;
		if(!isFile)
		{
			mBlocksInDirectories += size;
		}
	}
	catch(...)
	{
		// Error caught, not a good file then, let it be deleted
		return false;
	}
	
	// Got a container ID? (ie check was successful)
	if(containerID == -1)
	{
		return false;
	}

	// Debugging for Sune Molgaard's issue with non-existent files being
	// detected as unattached and crashing later in CheckUnattachedObjects()
	if (ObjectID == 0x90c1a)
	{
		BOX_INFO("Adding ID " << BOX_FORMAT_OBJECTID(ObjectID) <<
			" contained by " << BOX_FORMAT_OBJECTID(containerID) <<
			" with size " << size << " and isFile " << isFile);
	}

	// Add to list of IDs known about
	AddID(ObjectID, containerID, size, isFile);

	// Report success
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckFile(int64_t, IOStream &)
//		Purpose: Do check on file, return original container ID
//			 if OK, or -1 on error
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
int64_t BackupStoreCheck::CheckFile(int64_t ObjectID, IOStream &rStream)
{
	// Check that it's not the root directory ID. Having a file as
	// the root directory would be bad.
	if(ObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		// Get that dodgy thing deleted!
		BOX_ERROR("Have file as root directory. This is bad.");
		return -1;
	}

	// Check the format of the file, and obtain the container ID
	int64_t originalContainerID = -1;
	if(!BackupStoreFile::VerifyEncodedFileFormat(rStream,
		0 /* don't want diffing from ID */,
		&originalContainerID))
	{
		// Didn't verify
		return -1;
	}

	return originalContainerID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckDirInitial(int64_t, IOStream &)
//		Purpose: Do initial check on directory, return container ID
//			 if OK, or -1 on error
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
int64_t BackupStoreCheck::CheckDirInitial(int64_t ObjectID, IOStream &rStream)
{
	// Simply attempt to read in the directory
	BackupStoreDirectory dir;
	dir.ReadFromStream(rStream, IOStream::TimeOutInfinite);

	// Check object ID
	if(dir.GetObjectID() != ObjectID)
	{
		// Wrong object ID
		return -1;
	}
	
	// Return container ID
	return dir.GetContainerID();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckDirectories()
//		Purpose: Check the directories
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CheckDirectories()
{
	// Phase 1 did this:
	//    Checked that all the directories are readable
	//    Built a list of all directories and files which exist on the store
	//
	// This phase will check all the files in the directories, make
	// a note of all directories which are missing, and do initial fixing.

	// The root directory is not contained inside another directory, so
	// it has no directory entry to scan, but we have to count it
	// somewhere, so we'll count it here.
	mNumDirectories++;

	// Scan all objects.
	for(Info_t::const_iterator i(mInfo.begin()); i != mInfo.end(); ++i)
	{
		IDBlock *pblock = i->second;
		int32_t bentries = (pblock == mpInfoLastBlock)?mInfoLastBlockEntries:BACKUPSTORECHECK_BLOCK_SIZE;
		
		for(int e = 0; e < bentries; ++e)
		{
			uint8_t flags = GetFlags(pblock, e);
			if(flags & Flags_IsDir)
			{
				// Found a directory. Read it in.
				std::string filename;
				StoreStructure::MakeObjectFilename(pblock->mID[e], mStoreRoot, mDiscSetNumber, filename, false /* no dir creation */);
				BackupStoreDirectory dir;
				{
					std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, filename));
					dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
				}
				
				// Flag for modifications
				bool isModified = false;
				
				// Check for validity
				if(dir.CheckAndFix())
				{
					// Wasn't quite right, and has been modified
					BOX_WARNING("Directory ID " <<
						BOX_FORMAT_OBJECTID(pblock->mID[e]) <<
						" has bad structure");
					++mNumberErrorsFound;
					isModified = true;
				}
				
				// Go through, and check that everything in that directory exists and is valid
				std::vector<int64_t> toDelete;
				
				BackupStoreDirectory::Iterator i(dir);
				BackupStoreDirectory::Entry *en = 0;
				while((en = i.Next()) != 0)
				{
					// Lookup the item
					int32_t iIndex;
					IDBlock *piBlock = LookupID(en->GetObjectID(), iIndex);
					bool badEntry = false;
					if(piBlock != 0)
					{
						badEntry = !CheckDirectoryEntry(
							*en, pblock->mID[e],
							iIndex, isModified);
					}
					else
					{
						// Item can't be found. Is it a directory?
						if(en->IsDir())
						{
							// Store the directory for later attention
							mDirsWhichContainLostDirs[en->GetObjectID()] = pblock->mID[e];
						}
						else
						{
							// Just remove the entry
							badEntry = true;
							BOX_WARNING("Directory ID " << BOX_FORMAT_OBJECTID(pblock->mID[e]) << " references object " << BOX_FORMAT_OBJECTID(en->GetObjectID()) << " which does not exist.");
						}
					}
					
					// Is this entry worth keeping?
					if(badEntry)
					{
						toDelete.push_back(en->GetObjectID());
					}
					else if (en->IsFile())
					{
						// Add to sizes?
						if(en->IsOld())
						{
							mBlocksInOldFiles += en->GetSizeInBlocks();
						}
						if(en->IsDeleted())
						{
							mBlocksInDeletedFiles += en->GetSizeInBlocks();
						}
						if(!en->IsOld() &&
							!en->IsDeleted())
						{
							mBlocksInCurrentFiles += en->GetSizeInBlocks();
						}
					}
				}
				
				if(toDelete.size() > 0)
				{
					// Delete entries from directory
					for(std::vector<int64_t>::const_iterator d(toDelete.begin()); d != toDelete.end(); ++d)
					{
						dir.DeleteEntry(*d);
					}
					
					// Mark as modified
					isModified = true;
					
					// Check the directory again, now that entries have been removed
					dir.CheckAndFix();
					
					// Errors found
					++mNumberErrorsFound;
				}
				
				if(isModified && mFixErrors)
				{	
					BOX_WARNING("Fixing directory ID " << BOX_FORMAT_OBJECTID(pblock->mID[e]));

					// Save back to disc
					RaidFileWrite fixed(mDiscSetNumber, filename);
					fixed.Open(true /* allow overwriting */);
					dir.WriteToStream(fixed);
					// Commit it
					fixed.Commit(true /* convert to raid representation now */);
				}
			}
		}
	}

}

bool BackupStoreCheck::CheckDirectoryEntry(BackupStoreDirectory::Entry& rEntry,
	int64_t DirectoryID, int32_t IndexInDirBlock, bool& rIsModified)
{
	IDBlock *piBlock = LookupID(rEntry.GetObjectID(), IndexInDirBlock);
	ASSERT(piBlock != 0);

	uint8_t iflags = GetFlags(piBlock, IndexInDirBlock);
	bool badEntry = false;
	
	// Is the type the same?
	if(((iflags & Flags_IsDir) == Flags_IsDir) != rEntry.IsDir())
	{
		// Entry is of wrong type
		BOX_WARNING("Directory ID " <<
			BOX_FORMAT_OBJECTID(DirectoryID) <<
			" references object " <<
			BOX_FORMAT_OBJECTID(rEntry.GetObjectID()) <<
			" which has a different type than expected.");
		badEntry = true;
	}
	// Check that the entry is not already contained.
	else if(iflags & Flags_IsContained)
	{
		BOX_WARNING("Directory ID " <<
			BOX_FORMAT_OBJECTID(DirectoryID) <<
			" references object " <<
			BOX_FORMAT_OBJECTID(rEntry.GetObjectID()) <<
			" which is already contained.");
		badEntry = true;
	}
	else
	{
		// Not already contained -- mark as contained
		SetFlags(piBlock, IndexInDirBlock, iflags | Flags_IsContained);
		
		// Check that the container ID of the object is correct
		if(piBlock->mContainer[IndexInDirBlock] != DirectoryID)
		{
			// Needs fixing...
			if(iflags & Flags_IsDir)
			{
				// Add to will fix later list
				BOX_WARNING("Directory ID " <<
					BOX_FORMAT_OBJECTID(rEntry.GetObjectID())
					<< " has wrong container ID.");
				mDirsWithWrongContainerID.push_back(rEntry.GetObjectID());
			}
			else
			{
				// This is OK for files, they might move
				BOX_WARNING("File ID " <<
					BOX_FORMAT_OBJECTID(rEntry.GetObjectID())
					<< " has different container ID, "
					"probably moved");
			}
			
			// Fix entry for now
			piBlock->mContainer[IndexInDirBlock] = DirectoryID;
		}
	}
	
	// Check the object size, if it's OK and a file
	if(!badEntry && !rEntry.IsDir())
	{
		if(rEntry.GetSizeInBlocks() != piBlock->mObjectSizeInBlocks[IndexInDirBlock])
		{
			// Wrong size, correct it.
			rEntry.SetSizeInBlocks(piBlock->mObjectSizeInBlocks[IndexInDirBlock]);

			// Mark as changed
			rIsModified = true;

			// Tell user
			BOX_WARNING("Directory ID " <<
				BOX_FORMAT_OBJECTID(DirectoryID) <<
				" has wrong size for object " <<
				BOX_FORMAT_OBJECTID(rEntry.GetObjectID()));
		}
	}

	if (!badEntry)
	{
		if(rEntry.IsDir())
		{
			mNumDirectories++;
		}
		else
		{
			mNumFiles++;

			if(rEntry.IsDeleted())
			{
				mNumDeletedFiles++;
			}

			if(rEntry.IsOld())
			{
				mNumOldFiles++;
			}
		}
	}

	return !badEntry;
}

