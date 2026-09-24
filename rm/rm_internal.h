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

//
// Bitmap helper functions
//

inline int RM_BitmapByte(int slotNum)
{
    return slotNum / 8;
}

inline int RM_BitmapBit(int slotNum)
{
    return slotNum % 8;
}

inline bool RM_IsSlotOccupied(const char *bitmap, int slotNum)
{
    const int byteIndex = RM_BitmapByte(slotNum);
    const int bitIndex  = RM_BitmapBit(slotNum);

    return (static_cast<unsigned char>(bitmap[byteIndex]) &
            (1u << bitIndex)) != 0;
}

inline void RM_SetSlotOccupied(char *bitmap, int slotNum)
{
    const int byteIndex = RM_BitmapByte(slotNum);
    const int bitIndex  = RM_BitmapBit(slotNum);

    bitmap[byteIndex] =
        static_cast<char>(
            static_cast<unsigned char>(bitmap[byteIndex]) |
            (1u << bitIndex)
        );
}

inline void RM_SetSlotFree(char *bitmap, int slotNum)
{
    const int byteIndex = RM_BitmapByte(slotNum);
    const int bitIndex  = RM_BitmapBit(slotNum);

    bitmap[byteIndex] =
        static_cast<char>(
            static_cast<unsigned char>(bitmap[byteIndex]) &
            ~(1u << bitIndex)
        );
}

//
// Calculate the number of bytes required by the slot bitmap.
//
// One bit represents one record slot.
//
inline int RM_BitmapSize(int numRecords)
{
    return (numRecords + 7) / 8;
}

//
// Calculate the byte offset at which records begin.
//
inline int RM_RecordDataOffset(int numRecords, int bitmapSize)
{
    return static_cast<int>(sizeof(RM_FileHandle::RM_PageHdr))
           + bitmapSize;
}

//
// Calculate the maximum number of records that can fit in a page.
//
// The page contains:
//
//   RM_PageHdr
//   bitmap
//   record data
//
// The bitmap size depends on the number of records, so the calculation
// is performed iteratively until the complete layout fits.
//
inline int RM_ComputeRecordsPerPage(int recordSize)
{
    if (recordSize <= 0)
        return 0;

    int numRecords =
        (PF_PAGE_SIZE - static_cast<int>(
            sizeof(RM_FileHandle::RM_PageHdr))) / recordSize;

    while (numRecords > 0) {
        const int bitmapSize = RM_BitmapSize(numRecords);

        const int requiredSize =
            static_cast<int>(sizeof(RM_FileHandle::RM_PageHdr))
            + bitmapSize
            + numRecords * recordSize;

        if (requiredSize <= PF_PAGE_SIZE)
            break;

        --numRecords;
    }

    return numRecords;
}

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