// File: rm_filescan.cc
// Description: Implements the RM_FileScan class.
// This class provides clients the capability to perform
// scans over the records of an RM component file
// where a scan may be based on a specified condition.


#include "rm.h"
#include <cstring>
#include <cstdio>


RM_FileScan::RM_FileScan()
    : fileHandle(nullptr) ,
      attrType(INT).
      attrLength(0),
      attrOffset(0),
      compOp(NO_OP),
      scanOpened(false),
      currentPageNum(1),
      currentSlotNum(0),
      recordSize(0),
      nullableIndex(-1)
{
    value.stringVal = nullptr;
}

RM_FileScan::~RM_FileScan(){
    CloseScan();
}


bool RM_FileScan::checkSatisfy( char *data, bool isnull)
{
    // NO_OP means every record satisfies the condition.
    if (compOp == NO_OP)
        return true;

    // A NULL attribute does not satisfy a normal comparison.
    if (isnull)
        return false;

    if (data == nullptr)
        return false;

    // The attribute starts at attrOffset inside the record.
    char *attrData = data + attrOffset;


    // INT

    if (attrType == INT)
    {
        if (attrLength != static_cast<int>(sizeof(int)))
            return false;

        int actual = 0;
        std::memcpy(&actual, attrData, sizeof(int));

        switch (compOp)
        {
            case EQ_OP:
                return actual == value.intVal;

            case NE_OP:
                return actual != value.intVal;

            case LT_OP:
                return actual < value.intVal;

            case GT_OP:
                return actual > value.intVal;

            case LE_OP:
                return actual <= value.intVal;

            case GE_OP:
                return actual >= value.intVal;

            case NO_OP:
                return true;

            default:
                return false;
        }
    }


    // FLOAT

    if (attrType == FLOAT)
    {
        if (attrLength != static_cast<int>(sizeof(float)))
            return false;

        float actual = 0.0f;
        std::memcpy(&actual, attrData, sizeof(float));

        switch (compOp)
        {
            case EQ_OP:
                return actual == value.floatVal;

            case NE_OP:
                return actual != value.floatVal;

            case LT_OP:
                return actual < value.floatVal;

            case GT_OP:
                return actual > value.floatVal;

            case LE_OP:
                return actual <= value.floatVal;

            case GE_OP:
                return actual >= value.floatVal;

            case NO_OP:
                return true;

            default:
                return false;
        }
    }


    // STRING

    if (attrType == STRING)
    {
        if (value.stringVal == nullptr)
            return false;

        /*
         * Strings are compared using exactly attrLength bytes.
         *
         * This is preferable to strcmp() because an RM record's
         * string field does not necessarily have to be
         * null-terminated.
         */
        int comparison = std::memcmp(
            attrData,
            value.stringVal,
            attrLength
        );

        switch (compOp)
        {
            case EQ_OP:
                return comparison == 0;

            case NE_OP:
                return comparison != 0;

            case LT_OP:
                return comparison < 0;

            case GT_OP:
                return comparison > 0;

            case LE_OP:
                return comparison <= 0;

            case GE_OP:
                return comparison >= 0;

            case NO_OP:
                return true;

            default:
                return false;
        }
    }


    // Unknown attribute type.
    return false;
}

// Initialize a file scan over the records ib the open file referred 
// to by fileHandle.
RC RM_FileScan::OpenScan(const RM_FileHandle &filehandle,
                         AttrType attrType,
                         int attrLength,
                         int attrOffset,
                         CompOp compOp,
                         void *value,
                         ClientHint pinHint = NO_HINT)
{

    // Scan cannot be opened twice.
    if (scanOpened)
        return RM_SCANOPEN;


    // File must be open.
    if (!filehandle.IsOpen())
        return RM_INVALIDFILE;


    // Validate attribute type.
    if (attrType != INT &&
        attrType != FLOAT &&
        attrType != STRING)
    {
        return RM_INVALIDATTR;
    }


    // Validate attribute length.
    //
    // INT and FLOAT must have their natural sizes.
    // STRING may have any positive length that fits in
    // the record.
    if (attrLength <= 0)
        return RM_INVALIDATTR;

    if (attrType == INT &&
        attrLength != static_cast<int>(sizeof(int)))
    {
        return RM_INVALIDATTR;
    }

    if (attrType == FLOAT &&
        attrLength != static_cast<int>(sizeof(float)))
    {
        return RM_INVALIDATTR;
    }


    // Validate attribute offset.
    if (attrOffset < 0)
        return RM_INVALIDATTR;

    if (attrOffset + attrLength > filehandle.hdr.recordSize)
        return RM_INVALIDATTR;


    // Validate comparison operator.
    if (compOp != NO_OP &&
        compOp != EQ_OP &&
        compOp != NE_OP &&
        compOp != LT_OP &&
        compOp != GT_OP &&
        compOp != LE_OP &&
        compOp != GE_OP)
    {
        return RM_INVALIDATTR;
    }


    // A comparison value is required unless NO_OP is used.
    if (compOp != NO_OP && value == nullptr)
        return RM_INVALIDATTR;


    // Store scan configuration.
    this->fileHandle = &filehandle;
    this->attrType = attrType;
    this->attrLength = attrLength;
    this->attrOffset = attrOffset;
    this->compOp = compOp;

    this->recordSize =
        static_cast<short>(filehandle.hdr.recordSize);

    this->currentPageNum = 1;
    this->currentSlotNum = 0;
    this->nullableIndex = -1;


    // Copy comparison value.
    //
    // INT and FLOAT are copied directly.
    // STRING requires its own allocation because the scan
    // must continue to own the value after OpenScan() returns.
    if (attrType == INT)
    {
        if (value != nullptr)
        {
            std::memcpy(
                &this->value.intVal,
                value,
                sizeof(int)
            );
        }
    }
    else if (attrType == FLOAT)
    {
        if (value != nullptr)
        {
            std::memcpy(
                &this->value.floatVal,
                value,
                sizeof(float)
            );
        }
    }
    else if (attrType == STRING)
    {
        this->value.stringVal = nullptr;

        if (value != nullptr)
        {
            this->value.stringVal =
                new (std::nothrow) char[attrLength];

            if (this->value.stringVal == nullptr)
            {
                this->fileHandle = nullptr;
                return RM_NOMEM;
            }

            std::memcpy(
                this->value.stringVal,
                value,
                attrLength
            );
        }
    }


    // The scan is now open.
    (void)pinHint;

    scanOpened = true;

    return 0;
}                        

// GetNextRec
//
// Return the next record satisfying the scan condition.
//
// Scan order:
//
//     page 1, slot 0
//     page 1, slot 1
//     ...
//     page 1, last slot
//     page 2, slot 0
//     ...
//
// currentPageNum/currentSlotNum are deliberately updated
// before returning a matching record so the next call resumes
// at the following slot.

RC RM_FileScan::GetNextRec(RM_Record &rec)
{
    if (!scanOpened)
        return RM_INVALIDSCAN;


    if (fileHandle == nullptr)
        return RM_INVALIDSCAN;


    // Scan all data pages.
    //
    // Page 0 is the RM header page, so data pages start at 1.
    while (currentPageNum <= fileHandle->hdr.numPages)
    {
        PF_PageHandle ph;
        char *pPageData = nullptr;

        RC rc = fileHandle->GetPageData(
            currentPageNum,
            pPageData,
            ph
        );

        if (rc != 0)
            return rc;


        // Scan every slot on this page.
    
        while (currentSlotNum < fileHandle->hdr.numRecordsPerPage){
            SlotNum slotNum = currentSlotNum;

            // Advance immediately.
            // This is important: if this record matches,
            // we return from this function. The next call
            // must start at the following slot.
            currentSlotNum++;


            // Empty slot: nothing to scan.
            if (!fileHandle->IsSlotOccupied(
                    pPageData,
                    slotNum))
            {
                continue;
            }


            // Get pointer to the record.
            char *pRecordData = nullptr;

            rc = fileHandle->GetSlotPtr(
                pPageData,
                slotNum,
                pRecordData
            );

            if (rc != 0)
            {
                fileHandle->pfHandle.UnpinPage(
                    currentPageNum
                );

                return rc;
            }


            // Check whether this record satisfies the scan.
            //
            // The supplied RM layout does not define a NULL
            // bitmap, so the attribute is currently treated
            // as non-NULL.
            bool satisfies =
                checkSatisfy(pRecordData, false);

            if (!satisfies)
            {
                continue;
            }


            // Matching record.
            RID rid(
                currentPageNum,
                slotNum
            );

            rec.Set(
                pRecordData,
                fileHandle->hdr.recordSize,
                rid
            );


            // We are done with the page.
            fileHandle->pfHandle.UnpinPage(
                currentPageNum
            );

            return 0;
        }


        // No more slots on this page.
        //
        // Release the page before moving to the next one.

        rc = fileHandle->pfHandle.UnpinPage(
            currentPageNum
        );

        if (rc != 0)
            return rc;


        // Move to next page.
        currentPageNum++;
        currentSlotNum = 0;
    }


    // No more records.

    return RM_EOF;
}


RC RM_FileScan::Closescan(){
    if(!scanOpened)
        return RM_INVALIDSCAN;

    // Free string allocated memory. 
    // String comparison values are dynamically allocated in OpenScan
    if (attrType == STRING && value.stringVal != nullptr){
        delete[] value.stringVal;
        value.stringVal = nullptr;
    }

    // reset scan state
    fileHandle = nullptr;

    attrLength = 0;
    attrOffset = 0;

    scanOpened = false;

    currentPageNum = 1;
    currentSlotNum = 0;

    recordSize = 0;
    nullableIndex = -1;

    return 0;
}