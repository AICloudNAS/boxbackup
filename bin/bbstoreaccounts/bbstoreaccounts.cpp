// --------------------------------------------------------------------------
//
// File
//		Name:    bbstoreaccounts
//		Purpose: backup store administration tool
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <limits.h>
#include <stdio.h>

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif

#include <sys/types.h>

#include "box_getopt.h"
#include "BackupAccountControl.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConfigVerify.h"
#include "BackupStoreInfo.h"
#include "BoxPortsAndFiles.h"
#include "HousekeepStoreAccount.h"
#include "MainHelper.h"
#include "RaidFileController.h"
#include "StoreStructure.h"
#include "UnixUser.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

#include <cstring>

int PrintUsageAndExit()
{
	printf(
"Usage: bbstoreaccounts [-c config_file] action account_id [args]\n"
"Account ID is integer specified in hex\n"
"\n"
"Commands (and arguments):\n"
"  create <account> <discnum> <softlimit> <hardlimit>\n"
"        Creates the specified account number (in hex with no 0x) on the\n"
"        specified raidfile disc set number (see raidfile.conf for valid\n"
"        set numbers) with the specified soft and hard limits (in blocks\n"
"        if suffixed with B, MB with M, GB with G)\n"
"  info [-m] <account>\n"
"        Prints information about the specified account including number\n"
"        of blocks used. The -m option enable machine-readable output.\n"
"  enabled <accounts> <yes|no>\n"
"        Sets the account as enabled or disabled for new logins.\n"
"  setlimit <accounts> <softlimit> <hardlimit>\n"
"        Changes the limits of the account as specified. Numbers are\n"
"        interpreted as for the 'create' command (suffixed with B, M or G)\n"
"  delete <account> [yes]\n"
"        Deletes the specified account. Prompts for confirmation unless\n"
"        the optional 'yes' parameter is provided.\n"
"  check <account> [fix] [quiet]\n"
"        Checks the specified account for errors. If the 'fix' option is\n"
"        provided, any errors discovered that can be fixed automatically\n"
"        will be fixed. If the 'quiet' option is provided, less output is\n"
"        produced.\n"
"  name <account> <new name>\n"
"        Changes the \"name\" of the account to the specified string.\n"
"        The name is purely cosmetic and intended to make it easier to\n"
"        identify your accounts.\n"
"  housekeep <account>\n"
"        Runs housekeeping immediately on the account. If it cannot be locked,\n"
"        bbstoreaccounts returns an error status code (1), otherwise success\n"
"        (0) even if any errors were fixed by housekeeping.\n"
	);
	return 2;
}

int main(int argc, const char *argv[])
{
	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbstoreaccounts.memleaks",
		"bbstoreaccounts")

	MAINHELPER_START

	Logging::SetProgramName("bbstoreaccounts");

	// Filename for configuration file?
	std::string configFilename = BOX_GET_DEFAULT_BBSTORED_CONFIG_FILE;
	int logLevel = Log::EVERYTHING;
	bool machineReadableOutput = false;
	bool amazon_S3_mode = false;
	
	// See if there's another entry on the command line
	int c;
	while((c = getopt(argc, (char * const *)argv, "c:W:m")) != -1)
	{
		switch(c)
		{
		case 'c':
			// store argument
			configFilename = optarg;
			break;
		
		case 'W':
			logLevel = Logging::GetNamedLevel(optarg);
			if(logLevel == Log::INVALID)
			{
				BOX_FATAL("Invalid logging level: " << optarg);
				return 2;
			}
			break;

		case 'm':
			// enable machine readable output
			machineReadableOutput = true;
			break;

		case '?':
		default:
			return PrintUsageAndExit();
		}
	}

	Logging::FilterConsole((Log::Level) logLevel);
	Logging::FilterSyslog (Log::NOTHING);

	// Adjust arguments
	argc -= optind;
	argv += optind;

	// We should have at least one argument at this point.
	if(argc < 1)
	{
		return PrintUsageAndExit();
	}
	std::string command = argv[0];
	argv++;
	argc--;

	// Read in the configuration file
	std::string errs;
	std::auto_ptr<Configuration> config;
	if(!amazon_S3_mode)
	{
		if(configFilename.empty())
		{
			configFilename = BOX_GET_DEFAULT_BBSTORED_CONFIG_FILE;
		}
		config = Configuration::LoadAndVerify
			(configFilename, &BackupConfigFileVerify, errs);
	}

	if(config.get() == 0 || !errs.empty())
	{
		BOX_ERROR("Invalid configuration file " << configFilename <<
			":" << errs);
	}

	std::auto_ptr<BackupStoreAccountControl> apStoreControl;
	BackupAccountControl* pControl;

	if(amazon_S3_mode)
	{
		return PrintUsageAndExit();
	}
	else
	{
		// Initialise the raid file controller. Not needed in Amazon S3 mode.
		RaidFileController &rcontroller(RaidFileController::GetController());
		rcontroller.Initialise(config->GetKeyValue("RaidFileConf").c_str());

		// Get the Account ID (in hex without the leading 0x).
		int32_t id;
		if(::sscanf(argv[0], "%x", &id) != 1)
		{
			return PrintUsageAndExit();
		}
		argv++;
		argc--;

		apStoreControl.reset(new BackupStoreAccountControl(*config, id,
			machineReadableOutput));
		pControl = apStoreControl.get();
	}

	BackupAccountControl& control(*pControl);

	// Now do the command.
	if(command == "create")
	{
		// which disc?
		int32_t discnum;

		if(!amazon_S3_mode)
		{
			if(argc != 3 || ::sscanf(argv[0], "%d", &discnum) != 1)
			{
				BOX_ERROR("create requires raid file disc number, "
					"soft and hard limits.");
				return 2;
			}
		}

		// Create the account...
		if(!amazon_S3_mode)
		{
			int blocksize = apStoreControl->BlockSizeOfDiscSet(discnum);
			// Decode limits
			int32_t softlimit = pControl->SizeStringToBlocks(argv[1], blocksize);
			int32_t hardlimit = pControl->SizeStringToBlocks(argv[2], blocksize);
			return apStoreControl->CreateAccount(discnum, softlimit, hardlimit);
		}
	}
	else if(command == "info")
	{
		// Print information on this account
		return control.PrintAccountInfo();
	}
	else if(command == "enabled")
	{
		// Change the AccountEnabled flag on this account
		if(argc != 1)
		{
			return PrintUsageAndExit();
		}
		
		bool enabled = true;
		std::string enabled_string = argv[0];
		if(enabled_string == "yes")
		{
			enabled = true;
		}
		else if(enabled_string == "no")
		{
			enabled = false;
		}
		else
		{
			return PrintUsageAndExit();
		}

		return control.SetAccountEnabled(enabled);
	}
	else if(command == "setlimit")
	{
		// Change the limits on this account
		if(argc != 2)
		{
			BOX_ERROR("setlimit requires soft and hard limits.");
			return 2;
		}

		return control.SetLimit(argv[0], argv[1]);
	}
	else if(command == "name")
	{
		// Change the limits on this account
		if(argc != 1)
		{
			BOX_ERROR("name command requires a new name.");
			return 1;
		}

		return control.SetAccountName(argv[0]);
	}
	else if(command == "delete")
	{
		// Delete an account
		bool askForConfirmation = true;
		if(argc >= 1 && (::strcmp(argv[0], "yes") == 0))
		{
			askForConfirmation = false;
		}
		return apStoreControl->DeleteAccount(askForConfirmation);
	}
	else if(command == "check")
	{
		bool fixErrors = false;
		bool quiet = false;
		
		// Look at other options
		for(int o = 0; o < argc; ++o)
		{
			if(::strcmp(argv[o], "fix") == 0)
			{
				fixErrors = true;
			}
			else if(::strcmp(argv[o], "quiet") == 0)
			{
				quiet = true;
			}
			else
			{
				BOX_ERROR("Unknown option " << argv[o] << ".");
				return 2;
			}
		}
	
		// Check the account
		return apStoreControl->CheckAccount(fixErrors, quiet);
	}
	else if(command == "housekeep")
	{
		return apStoreControl->HousekeepAccountNow();
	}
	else
	{
		BOX_ERROR("Unknown command '" << command << "'.");
		return 2;
	}

	return 0;

	MAINHELPER_END
}

