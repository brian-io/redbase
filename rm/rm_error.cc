//
// File:        rm_error.cc
// Description: RM_PrintError function
//

#include <cerrno>
#include <cstring>
#include <iostream>

#include "rm_internal.h"

using namespace std;

void RM_PrintError(RC rc)
{
    switch (rc) {
        case RM_EOF:
            cerr << "RM warning: end of file\n";
            break;

        case RM_INVALIDRID:
            cerr << "RM error: invalid RID\n";
            break;

        case RM_RECORDNOTFOUND:
            cerr << "RM error: record not found\n";
            break;

        case RM_INVALIDRECORD:
            cerr << "RM error: invalid record\n";
            break;

        case RM_INVALIDFILE:
            cerr << "RM error: file handle is not open\n";
            break;

        case RM_INVALIDSCAN:
            cerr << "RM error: scan is not open\n";
            break;

        case RM_RECORDSIZETOOLARGE:
            cerr << "RM error: record size is too large\n";
            break;

        case RM_INVALIDRECORDSIZE:
            cerr << "RM error: invalid record size\n";
            break;

        case RM_INVALIDATTR:
            cerr << "RM error: invalid attribute\n";
            break;

        case RM_SCANOPEN:
            cerr << "RM error: scan is already open\n";
            break;

        case RM_NOMEM:
            cerr << "RM error: memory allocation failure\n";
            break;

        case RM_PAGECORRUPT:
            cerr << "RM error: page data is corrupted\n";
            break;

        case RM_UNEXPECTEDRC:
            cerr << "RM error: unexpected PF return code\n";
            break;

        case 0:
            cerr << "RM_PrintError called with return code of 0\n";
            break;

        default:
            cerr << "RM error: " << rc << " is out of bounds\n";
            break;
    }
}