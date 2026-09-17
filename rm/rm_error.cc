//
// File:        rm_error.cc
// Description: RM_PrintError function
//

#include <cerrno>
#include <cstdio>
#include <iostream>
#include "rm_internal.h"

using namespace std;

//
// Error table
//
static char *RM_WarnMsg[] = {
 
};

static char *RM_ErrorMsg[] = {
  
};

//
// RM_PrintError
//
// Desc: Send a message corresponding to a RM return code to cerr
//       Assumes RM_UNIX is last valid RM return code
// In:   rc - return code for which a message is desired
//
void RM_PrintError(RC rc)
{
  // Check the return code is within proper limits
  if (rc >= START_RM_WARN && rc <= RM_LASTWARN)
    // Print warning
    cerr << "RM warning: " << RM_WarnMsg[rc - START_RM_WARN] << "\n";
  // Error codes are negative, so invert everything
  else if (-rc >= -START_RM_ERR && -rc < -RM_LASTERROR)
    // Print error
    cerr << "RM error: " << RM_ErrorMsg[-rc + START_RM_ERR] << "\n";
  else if (rc == RM_UNIX)
#ifdef PC
      cerr << "OS error\n";
#else
      cerr << strerror(errno) << "\n";
#endif
  else if (rc == 0)
    cerr << "RM_PrintError called with return code of 0\n";
  else
    cerr << "RM error: " << rc << " is out of bounds\n";
}