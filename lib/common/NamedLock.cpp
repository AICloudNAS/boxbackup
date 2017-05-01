// --------------------------------------------------------------------------
//
// File
//		Name:    NamedLock.cpp
//		Purpose: A global named lock, implemented as a lock file in
//			 file system
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#ifdef HAVE_FLOCK
	#include <sys/file.h>
#endif

#include "CommonException.h"
#include "NamedLock.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    NamedLock::NamedLock()
//		Purpose: Constructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
NamedLock::NamedLock()
: mFileDescriptor(INVALID_FILE)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    NamedLock::~NamedLock()
//		Purpose: Destructor (automatically unlocks if locked)
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
NamedLock::~NamedLock()
{
	if(mFileDescriptor != INVALID_FILE)
	{
		ReleaseLock();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    NamedLock::TryAndGetLock(const char *, int)
//		Purpose: Tries to get a lock on the name in the file system.
//			 IMPORTANT NOTE: If a file exists with this name, it
//			 will be deleted.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
bool NamedLock::TryAndGetLock(const std::string& rFilename, int mode)
{
	// Check
	if(mFileDescriptor != INVALID_FILE)
	{
		THROW_FILE_ERROR("Named lock already in use", rFilename, CommonException,
			NamedLockAlreadyLockingSomething);
	}

	mFileName = rFilename;

	// See if the lock can be got
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	std::string method_name;


#if HAVE_DECL_O_EXLOCK
	flags |= O_NONBLOCK | O_EXLOCK;
	method_name = "O_EXLOCK";
	mMethod = LOCKTYPE_O_EXLOCK;
#elif defined BOX_OPEN_LOCK
	flags |= BOX_OPEN_LOCK;
	method_name = "BOX_OPEN_LOCK";
	mMethod = LOCKTYPE_WIN32;
#elif HAVE_DECL_F_SETLK
	method_name = "no special flags (for F_SETLK)";
	mMethod = LOCKTYPE_F_SETLK;
#elif defined HAVE_FLOCK
	method_name = "no special flags (for flock())";
	mMethod = LOCKTYPE_FLOCK;
#else
	// We have no other way to get a lock, so all we can do is fail if the
	// file already exists, and take the risk of stale locks.
	flags |= O_EXCL;
	method_name = "O_EXCL";
	mMethod = LOCKTYPE_DUMB;
#endif

	BOX_TRACE("Trying to create lockfile " << rFilename << " using " << mMethod);

#ifdef WIN32
	HANDLE fd = openfile(rFilename.c_str(), flags, mode);
	if(fd == INVALID_HANDLE_VALUE)
#else
	int fd = ::open(rFilename.c_str(), flags, mode);
	if(fd == -1)
#endif
	{
		// Failed to open the file. What's the reason? The errno which indicates a lock
		// conflict depends on the locking method.

#if HAVE_DECL_O_EXLOCK
		ASSERT(mMethod == LOCKTYPE_O_EXLOCK);
		if(errno == EWOULDBLOCK)
#elif defined BOX_OPEN_LOCK
		ASSERT(mMethod == LOCKTYPE_WIN32);
		if(errno == EBUSY)
#else // DUMB, HAVE_DECL_F_SETLK or HAVE_FLOCK
		ASSERT(mMethod == LOCKTYPE_F_SETLK || mMethod == LOCKTYPE_FLOCK ||
			mMethod == LOCKTYPE_DUMB);
		// Exclude F_SETLK and FLOCK cases:
		if(mMethod == LOCKTYPE_DUMB && errno == EEXIST)
#endif
		{
			// Lockfile already exists, and we tried to open it
			// exclusively, which means we failed to lock it, which
			// means that it's locked by someone else, which is an
			// expected error condition, signalled by returning
			// false instead of throwing.
			BOX_NOTICE("Failed to lock lockfile with " << method_name << ": " <<
				rFilename << ": already locked by another process?");
			return false;
		}
		else
		{
			THROW_SYS_FILE_ERROR("Failed to open lockfile with " << method_name,
				rFilename, CommonException, OSFileError);
		}
	}

	try
	{
		if(mMethod == LOCKTYPE_FLOCK)
		{
#ifdef HAVE_FLOCK
			BOX_TRACE("Trying to lock lockfile " << rFilename << " using flock()");
			if(::flock(fd, LOCK_EX | LOCK_NB) != 0)
			{
				if(errno == EWOULDBLOCK)
				{
					::close(fd);
					BOX_NOTICE("Failed to lock lockfile with flock(): " << rFilename
						<< ": already locked by another process");
					return false;
				}
				else
				{
					THROW_SYS_FILE_ERROR("Failed to lock lockfile with flock()",
						rFilename, CommonException, OSFileError);
				}
			}
#endif
		}
		else if(mMethod == LOCKTYPE_F_SETLK)
		{
#if HAVE_DECL_F_SETLK
			struct flock desc;
			desc.l_type = F_WRLCK;
			desc.l_whence = SEEK_SET;
			desc.l_start = 0;
			desc.l_len = 0;
			BOX_TRACE("Trying to lock lockfile " << rFilename << " using fcntl()");
			if(::fcntl(fd, F_SETLK, &desc) != 0)
			{
				if(errno == EAGAIN)
				{
					::close(fd);
					BOX_NOTICE("Failed to lock lockfile with fcntl(): " << rFilename
						<< ": already locked by another process");
					return false;
				}
				else
				{
					THROW_SYS_FILE_ERROR("Failed to lock lockfile with fcntl()",
						rFilename, CommonException, OSFileError);
				}
			}
#endif
		}
	}
	catch(BoxException &e)
	{
#ifdef WIN32
		CloseHandle(fd);
#else
		::close(fd);
#endif
		THROW_FILE_ERROR("Failed to lock lockfile: " << e.what(), rFilename,
			CommonException, NamedLockFailed);
	}

	if(!FileExists(rFilename))
	{
		BOX_ERROR("Locked lockfile " << rFilename << ", but lockfile no longer "
			"exists, bailing out");
#ifdef WIN32
		CloseHandle(fd);
#else
		::close(fd);
#endif
		return false;
	}

	// Success
	mFileDescriptor = fd;
	BOX_TRACE("Successfully locked lockfile " << rFilename << " using " << method_name);

	return true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    NamedLock::ReleaseLock()
//		Purpose: Release the lock. Exceptions if the lock is not held
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void NamedLock::ReleaseLock()
{
	// Got a lock?
	if(mFileDescriptor == INVALID_FILE)
	{
		THROW_EXCEPTION(CommonException, NamedLockNotHeld)
	}

#ifndef WIN32
	// Delete the file. We need to do this before closing the filehandle,
	// if we used flock() or fcntl() to lock it, otherwise someone could
	// acquire the lock, release and delete it between us closing (and
	// hence releasing) and deleting it, and we'd fail when it came to
	// deleting the file. This happens in tests much more often than
	// you'd expect!
	//
	// This doesn't apply on systems using plain lockfile locking, such as
	// Windows, and there we need to close the file before deleting it,
	// otherwise the system won't let us delete it.

	if(EMU_UNLINK(mFileName.c_str()) != 0)
	{
		// Don't try to release it again
		close(mFileDescriptor);
		mFileDescriptor = -1;

		// In the case of all sensible lock types (all but LOCKTYPE_DUMB) and
		// LOCKTYPE_WIN32 (where this code is not called), it's possible to acquire the
		// same lock multiple times in the same process. We can't really tell if this has
		// happened until we try to delete the lockfile and the second time we do that
		// (unlocking the first lock) it throws an error. Removing a lockfile while open
		// is a bad idea, so we should raise an appropriate error message to debug this
		// and stop doing it.
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to delete lockfile"),
			CommonException, OSFileError);
	}
#endif // !WIN32

	// Close the file
#ifdef WIN32
	if(!CloseHandle(mFileDescriptor))
#else
	if(::close(mFileDescriptor) != 0)
#endif
	{
		// Don't try to release it again
		mFileDescriptor = INVALID_FILE;
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to close lockfile"),
			CommonException, OSFileError);
	}

	// Mark as unlocked, so we don't try to close it again if the unlink() fails.
	mFileDescriptor = INVALID_FILE;

#ifdef WIN32
	// On Windows we need to close the file before deleting it, otherwise
	// the system won't let us delete it.

	if(EMU_UNLINK(mFileName.c_str()) != 0)
	{
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to delete lockfile"),
			CommonException, OSFileError);
	}
#endif // WIN32

	BOX_TRACE("Released lock and deleted lockfile " << mFileName);
}
