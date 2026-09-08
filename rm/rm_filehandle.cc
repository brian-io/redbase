// Design notes
// ============
//
// File layout
// -----------
//   Page 0   : File header page
//   Pages 1..N : Data pages
//
// File header page format (stored in PF page 0):
//   FileHeader struct (see rm.h)
//
// Data page format:
//   RM_PageHdr struct
//   bitmap  (bitmapSize bytes)  — one bit per slot
//   records (recordSize bytes each)
//
// Free-page linked list
// ---------------------
//   The file header keeps the page number of the first data page that has at
//   least one free slot.  Each such page's RM_PageHdr keeps next/prev pointers
//   to form a doubly-linked list.  When a page becomes full it is removed from
//   the list.  When a page is created (AllocatePage) it is added to the front
//   of the list.  When a deletion opens a slot on an otherwise-full page, that
//   page is added back to the front of the list.
//
// Bitmap
// ------
//   Bit i of the bitmap = 1  → slot i is occupied
//   Bit i of the bitmap = 0  → slot i is free
//   The bitmap is stored as an array of unsigned chars starting immediately
//   after the RM_PageHdr.
//
// Offset arithmetic helper
// ------------------------
//   bitmap start  = pData + sizeof(RM_PageHdr)
//   slot  i start = pData + sizeof(RM_PageHdr) + bitmapSize + i * recordSize
//

#include "rm.h"
#include <cstring>

RM_FileHandle::RM_FileHandle() : bOpen(false), bHdrModified(false){
    // Initialize file header page
    memset(&hdr, 0, sizeof(hdr));
    hdr.firstFreePage = -1;
}

RM_FileHandle::~RM_FileHandle(){

}

// Offset of the bitmap for a given data page
static inline char *BitmapPtr (char *pData) {
    return pData + sizeof(RM_FileHandle::RM_PageHdr);
}

// Check if the bit slotNum is set in the bitmap
bool RM_FileHandle::IsSlotOccupied( const char *pData, SlotNum SlotNum) const {
    const unsigned char *bm = reinterpret_cast<const unsigned char*>(
        pData + sizeof(RM_PageHdr));
    return (bm[SlotNum / 8] & (1u << (SlotNum % 8))) != 0;

}

// Set or clear the bit slotNum in the bitmap
void RM_FileHandle::SetSlotOccupied(char *pData, SlotNum slotNum, bool occupied)  {
    
    unsigned char *bm = reinterpret_cast<unsigned char *>(
        pData + sizeof(RM_PageHdr)
    );

    if (occupied) {
        bm[slotNum / 8] |= (1u << (slotNum % 8));
    }else {
        bm[slotNum / 8] &= (1u << (slotNum % 8));
    }
}

// Return pointer to record slot i within page data
RC RM_FileHandle::GetSlotPtr(char *pData, SlotNum slotNum, char *&pSlot) const {
    
    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage)
        return RM_INVALIDRID;

    pSlot = pData + sizeof(RM_PageHdr) + hdr.bitmapSize + slotNum * hdr.recordSize;

    return 0;
}

// Scan bitmap to find the first free slot. Return RM_EOF if none.
RC RM_FileHandle::FindFreeSlot(char *pData, SlotNum &slotNum) const {
    
    for (int i = 0; i < hdr.numRecordsPerPage; i++) {
        if (!IsSlotOccupied(pData, i)) {
            slotNum = i;
            return 0;
        }
    }

    return RM_EOF; // Page is full
}

// Count the occupied slots
int RM_FileHandle::NumSlotsOccupied(const char *pData) const {
    int count = 0;

    for (int i = 0; i < hdr.numRecordsPerPage; i++){
        if (IsSlotOccupied(pData, i)) count++;
    }

    return count;
}

bool RM_FileHandle::PageHasFreeSlot(const char *pData) const {
    
    RM_PageHdr *pHdr = (RM_PageHdr *)pData;

    return pHdr->numRecords < hdr.numRecordsPerPage;
    
}


// Remove a page from the free-page doubly-linked list
void RM_FileHandle::RemoveFromFreeList(PageNum pageNum) {
    // Read the page header
    PF_PageHandle ph;
    if (pfHandle.GetThisPage(pageNum, ph) != 0) return;

    char *pData;
    ph.GetData(pData);

    RM_PageHdr *pgh = reinterpret_cast<RM_PageHdr*>(pData);

    PageNum prev = pgh->prevFreePage;
    PageNum next = pgh->nextFreePage;

    pgh->prevFreePage = -1;
    pgh->nextFreePage = -1;

    pfHandle.MarkDirty(pageNum);
    pfHandle.UnpinPage(pageNum);

    // Update prev's next
    if (prev != -1) {
        PF_PageHandle prevPH;
        char *prevData;
        if(pfHandle.GetThisPage(prev, prevPH) == 0) {
           prevPH.GetData(prevData);
            reinterpret_cast<RM_PageHdr*>(prevData)->nextFreePage = next;
            pfHandle.MarkDirty(prev);
            pfHandle.UnpinPage(prev);
        }
    } else {
        // pageNum was the head
        hdr.firstFreePage = next;
        bHdrModified = true;
    }
 
    // Update next's prev
    if (next != -1) {
        PF_PageHandle nextPH;
        char *nextData;
        if (pfHandle.GetThisPage(next, nextPH) == 0) {
            nextPH.GetData(nextData);
            reinterpret_cast<RM_PageHdr*>(nextData)->prevFreePage = prev;
            pfHandle.MarkDirty(next);
            pfHandle.UnpinPage(next);
        }
    }
}

// Insert a page at the front of the free-page list
void RM_FileHandle::AddToFreeList(PageNum pageNum) {
    
    
    PF_PageHandle ph;
    if (pfHandle.GetThisPage(pageNum, ph) != 0) return;
    char *pData;
    ph.GetData(pData);

    RM_PageHdr *pgh = reinterpret_cast<RM_PageHdr*>(pData);
 
    PageNum oldHead = hdr.firstFreePage;

    pgh->nextFreePage = oldHead;
    pgh->prevFreePage = -1;

    pfHandle.MarkDirty(pageNum);
    pfHandle.UnpinPage(pageNum);
 
    // Update old head's prevFreePage
    if (oldHead != -1) {
        PF_PageHandle headPH;
        char *headData;
        if (pfHandle.GetThisPage(oldHead, headPH) == 0) {
            headPH.GetData(headData);
            reinterpret_cast<RM_PageHdr*>(headData)->prevFreePage = pageNum;
            pfHandle.MarkDirty(oldHead);
            pfHandle.UnpinPage(oldHead);
        }
    }
 
    hdr.firstFreePage = pageNum;
    bHdrModified = true;
}
 
// Write the cached file header back to page 0
RC RM_FileHandle::WriteFileHeader() {
    
    PF_PageHandle ph;
    RC rc;

    if ((rc = pfHandle.GetThisPage(0, ph))) return rc;

    char *pData;
    ph.GetData(pData);

    memcpy(pData, &hdr, sizeof(RM_FileHdr));  

    pfHandle.MarkDirty(0);
    pfHandle.UnpinPage(0);

    bHdrModified = false;
    return 0;
}
 
// Find an existing page with a free slot, or allocate a new one.
RC RM_FileHandle::FindOrAllocatePage(PageNum &pageNum, PF_PageHandle &ph) {
    RC rc;
 
    if (hdr.firstFreePage != -1) {
        // Use the head of the free list
        pageNum = hdr.firstFreePage;
        if ((rc = pfHandle.GetThisPage(pageNum, ph))) return rc;
        return 0;
    }
 
    // No free page — allocate a new data page
    if ((rc = pfHandle.AllocatePage(ph))) return rc;

    char *pData;
    ph.GetData(pData);
    ph.GetPageNum(pageNum);
 
    // Initialise the new page header
    RM_PageHdr *pgh = reinterpret_cast<RM_PageHdr*>(pData);

    pgh->numRecords  = 0;
    pgh->nextFreePage = -1;
    pgh->prevFreePage = -1;
    memset(BitmapPtr(pData), 0, hdr.bitmapSize);
 
    hdr.numPages++;
    bHdrModified = true;
 
    // Keep pinned for caller; AddToFreeList will unpin via GetThisPage internally.
    // We need to unpin first so AddToFreeList can re-pin.
    pfHandle.MarkDirty(pageNum);
    pfHandle.UnpinPage(pageNum);
 
    // Add new page to free list then re-pin for the caller
    AddToFreeList(pageNum);
    if ((rc = pfHandle.GetThisPage(pageNum, ph))) return rc;
 
    return 0;
}
 

RC RM_FileHandle::GetRec( const RID &rid, RM_Record &rec) const {
    // 1. Validate entries
    // 2. getRID -> Find page -> open file -> open page  -> fetch rec
    // 3. return rec then unpin page
    // 4. update page and file header if needed

    if (!bOpen) return RM_INVALIDFILE;

    RC rc;
    PageNum pageNum;
    SlotNum slotNum;
    if ((rc = rid.GetPageNum(pageNum))) return RM_INVALIDRID;
    if ((rc = rid.GetSlotNum(slotNum))) return RM_INVALIDRID;

    // page 0 is the header page; data pages start at 1
    if (pageNum < 1) return RM_INVALIDRID;

    // Validate against known page count (hdr.numPages)
    if (pageNum > hdr.numPages) return RM_INVALIDRID;
    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage) return RM_INVALIDRID;

    PF_PageHandle ph;
    if (( rc = pfHandle.GetThisPage(pageNum, ph))) return rc;

    char *pData;
    if (( rc = ph.GetData(pData))) return rc;

     if (!IsSlotOccupied(pData, slotNum)) {
        pfHandle.UnpinPage(pageNum);
        return RM_RECORDNOTFOUND;
    }
 
    char *pSlot;
    if ((rc = GetSlotPtr(pData, slotNum, pSlot))) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }
 
    rec.Set(pSlot, hdr.recordSize, rid);

    pfHandle.UnpinPage(pageNum);
    return 0;
}

RC RM_FileHandle::InsertRec(const char *pData, RID &rid ){

}


RC RM_FileHandle::DeleteRec(const RID &rid){

}


RC RM_FileHandle::UpdateRec(const RM_Record &rec){

}


RC RM_FileHandle::ForcePages(PageNum PageNum = ALL_PAGES){

}

RC RM_FileHandle::ReadHdr(){ 
    // open file but we expect it to already be opened
    //getfirstpage (page 0) -> pinpage -> copy contents to filehandle -> unpin and maybe close
    RC rc;
    PF_PageHandle ph;
    char *pData;

    if ((rc = pfHandle.GetFirstPage(ph))) return rc; // we could use GetThisPage(0, ph) but mmhe
   
    if ((rc = ph.GetData(pData))){
        pfHandle.UnpinPage(ph.GetPageNum)
    } // set pData 

    memcpy(pData, &hdr, sizeof(hdr))




}

RC RM_FileHandle::WriteHdr(){

}