//
// File:        rm_internal.h
// Description: Declarations internal to the record management component
//

#ifndef RM_INTERNAL_H
#define RM_INTERNAL_H

#include <cstdlib>
#include <cstring>

#include "../redbase.h"
#include "../pf/pf.h"
#include "rm.h"

//
// Constants
//

// Page 0 of an RM file contains the RM file header.
//
// The RM_FileHdr structure is copied into this page. The remainder
// of the page is unused/padding.
const int RM_FILE_HDR_SIZE = PF_PAGE_SIZE;

//
// Free-page list values.
//
// A page number >= 0 identifies another page in the RM free-page list.
// RM_PAGE_LIST_END indicates the end of the list.
//
#define RM_PAGE_LIST_END (-1)

//
// RM page layout
//
// Every RM data page has the following layout:
//
//   +-----------------------------+
//   | RM_PageHdr                  |
//   +-----------------------------+
//   | Slot occupancy bitmap       |
//   +-----------------------------+
//   | Record 0                    |
//   +-----------------------------+
//   | Record 1                    |
//   +-----------------------------+
//   | ...                         |
//   +-----------------------------+
//   | Record N                    |
//   +-----------------------------+
//
// The bitmap contains one bit per record slot:
//
//   0 = slot is free
//   1 = slot is occupied
//


// RM error-code ranges
//
// Warnings are positive. Errors are negative.
//

#define START_RM_WARN          RM_EOF
#define RM_LASTWARN            RM_SCANOPEN

#define START_RM_ERR           RM_NOMEM
#define RM_LASTERROR           RM_UNEXPECTEDRC

#endif // RM_INTERNAL_H