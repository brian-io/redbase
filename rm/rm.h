//
// rm.h
//
//   Record Manager component interface
//
// This file does not include the interface for the RID class.  This is
// found in rm_rid.h
//

#ifndef RM_H
#define RM_H

// Please DO NOT include any files other than redbase.h and pf.h in this
// file.  When you submit your code, the test program will be compiled
// with your rm.h and your redbase.h, along with the standard pf.h that
// was given to you.  Your rm.h, your redbase.h, and the standard pf.h
// should therefore be self-contained (i.e., should not depend upon
// declarations in any other file).

// Do not change the following includes
#include "../redbase.h"
#include "rm_rid.h"
#include "../pf/pf.h"

#include <stddef.h>

//
// RM_Record: RM Record interface. The RM Record object stores the record
// RID and copies of its contents. (The copies are kept in an array of
// chars the RM_Record mallocs )
//
class RM_Record {
    friend class RM_FileHandle;
    friend class RM_FileScan;

public:
    RM_Record ();
    ~RM_Record();

    RM_Record(const RM_Record& rec) = delete; // copy 

    RM_Record& operator=(const RM_Record&) = delete; // assignment operator

    // Return the data corresponding to the record.  Sets *pData to the
    // record contents.
    RC GetData(char *&pData) const;

    // Return the RID associated with the record
    RC GetRid (RID &rid) const;

private:
    friend class RM_FileHandle;
    friend class RM_FileScan;

    void Set(const char *pData, int size, const RID &rid); //  Internal setter

    char *pData;        // copy of the record's data
    int dataSize;       // size of the data in bytes
    bool valid;         // has a record been loaded?

    RID rid;            // record identifier
};




//
// RM_FileHandle: RM File interface
//
class RM_FileHandle {
    friend class RM_Manager;
    friend class RM_FileScan;

public:
    RM_FileHandle ();
    ~RM_FileHandle();

    // Given a RID, return the record
    RC GetRec     (const RID &rid, RM_Record &rec) const;

    // Insert a new record
    //   `isnull' gives the information for each nullable fields
    RC InsertRec  (const char *pData, RID &rid);

    RC DeleteRec  (const RID &rid);                    // Delete a record
    RC UpdateRec  (const RM_Record &rec);              // Update a record

    // Forces a page (along with any contents stored in this class)
    // from the buffer pool to disk.  Default value forces all pages.
    RC ForcePages (PageNum pageNum = ALL_PAGES);

    bool IsOpen() const { return bOpen; }


    // Page heaer at the start of each data page. Public for ComputePageLayout
    struct RM_PageHdr {
        int numRecords;  // number of occupied records on this page
        PageNum nextFreePage; // page number of the next page with free slots, or(-1)
        PageNum prevFreePage; // page number of the prev page with free slots, or (-1) -> prev page in free list
    };

     // File header information store in header page. Cached here
    struct RM_FileHdr {
        int recordSize;             // size of each record in bytes
        int numRecordsPerPage;      // max records per page
        int numPages;               // total number of pages excluding header
        int bitmapSize;             // size of the slot bitmap in bytes
        int pageDataOffset;         // offset into page where record data starts
        PageNum firstFreePage;      // pageNumber of first page with free slots (-1 if none)

    };

private:
    friend class RM_Manager;
    friend class RM_FileScan;
 
    // -----------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------
    RC GetPageData(PageNum pageNum, char *&pData, PF_PageHandle &ph) const;
    RC GetSlotPtr(char *pData, SlotNum slotNum, char *&pSlot) const;
    bool IsSlotOccupied(const char *pData, SlotNum slotNum) const;
    void SetSlotOccupied(char *pData, SlotNum slotNum, bool occupied);
    RC FindFreeSlot(char *pData, SlotNum &slotNum) const;
    RC FindOrAllocatePage(PageNum &pageNum, PF_PageHandle &ph);
    void RemoveFromFreeList(PageNum pageNum);
    void AddToFreeList(PageNum pageNum);
    bool PageHasFreeSlot(const char *pData) const;
    int  NumSlotsOccupied(const char *pData) const;
   


    PF_FileHandle pfHandle;         // underlying PF file handle
    RM_FileHdr hdr;                    // cached file header

    bool bOpen;                    // is the handle referencing an open file?
    bool bHdrModified;            // has the header been changed since it was opened?
   


    // Read header from page 0 into hdr
    RC ReadHdr();
    // Write header back to page 0
    RC WriteHdr();
};

//
// RM_FileScan: condition-based scan of records in the file
//
class RM_FileScan {
    const RM_FileHandle *fileHandle;
    AttrType attrType;
    int attrLength;
    int attrOffset;
    CompOp compOp;

    union {
        int intVal;
        float floatVal;
        char *stringVal;
    } value;

    bool scanOpened;
    PageNum currentPageNum;
    SlotNum currentSlotNum;
    short recordSize;
    int nullableIndex;

    bool checkSatisfy(char *data, bool isnull);
public:
    RM_FileScan  ();
    ~RM_FileScan ();

    RC OpenScan  (const RM_FileHandle &fileHandle,
                  AttrType   attrType,
                  int        attrLength,
                  int        attrOffset,
                  CompOp     compOp,
                  void       *value,
                  ClientHint pinHint = NO_HINT); // Initialize a file scan
    RC GetNextRec(RM_Record &rec);               // Get next matching record
    RC CloseScan ();                             // Close the scan
};

//
// RM_Manager: provides RM file management
//
class RM_Manager {
    public:
        RM_Manager    (PF_Manager &pfm);
        ~RM_Manager   ();

        RC CreateFile (const char *fileName, int recordSize);
        RC DestroyFile(const char *fileName);
        RC OpenFile   (const char *fileName, RM_FileHandle &fileHandle);

        RC CloseFile  (RM_FileHandle &fileHandle);


    private:
        PF_Manager &pfm; // reference to underlying program's PF_Manager

};

//
// Print-error function
//
void RM_PrintError(RC rc);

#define RM_EOF                  1   // End of file / scan - no more records
#define RM_INVALIDRID           2   // Invalid RID (page/slot out of range or empty)
#define RM_RECORDNOTFOUND       3   // Record not found (slot is empty)
#define RM_INVALIDRECORD        4   // RM_Record has no data loaded
#define RM_INVALIDFILE          5   // FileHandle does not refer to an open file
#define RM_INVALIDSCAN          6   // FileScan has not been opened
#define RM_RECORDSIZETOOLARGE   7   // Record size > PF_PAGE_SIZE
#define RM_INVALIDRECORDSIZE    8   // Record size <= 0
#define RM_INVALIDATTR          9   // Invalid attribute type/length/offset
#define RM_SCANOPEN             10  // Scan is already open
 
// Negative (unrecoverable)
#define RM_NOMEM                (-1)    // Memory allocation failure
#define RM_PAGECORRUPT          (-2)    // Page data appears corrupted
#define RM_UNEXPECTEDRC         (-3)    // Unexpected return code from PF

#endif // RM_H