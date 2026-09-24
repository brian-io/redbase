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

RM_FileHandle::RM_FileHandle() 
    : bOpen(false), 
      bHdrModified(false)
{
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

    return (bm[SlotNum / 8] & (1u << (SlotNum % 8))) ;

}

// Set or clear the bit slotNum in the bitmap
void RM_FileHandle::SetSlotOccupied(char *pData, SlotNum slotNum, bool occupied)  {
     
    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage)
        return ;

    unsigned char *bm = reinterpret_cast<unsigned char *>(
        pData + sizeof(RM_PageHdr)
    );

    if (occupied) {
        bm[slotNum / 8] |= (1u << (slotNum % 8));
    }else {
        bm[slotNum / 8] &= ~(1u << (slotNum % 8));
    }
}

// Return pointer to record slot i within page data
RC RM_FileHandle::GetSlotPtr(char *pData, SlotNum slotNum, char *&pSlot) const {
    
    if(pData == nullptr)
        return RM_PAGECORRUPT;

    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage)
        return RM_INVALIDRID;

    pSlot = pData 
            + hdr.pageDataOffset    // + sizeof(RM_PageHdr) + hdr.bitmapSize 
            + slotNum * hdr.recordSize;

    return 0;
}

// Scan bitmap to find the first free slot. Return RM_EOF if none.
RC RM_FileHandle::FindFreeSlot(char *pData, SlotNum &slotNum) const {
    
    if(pData == nullptr)
        return RM_PAGECORRUPT;


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
    
    if (pData == nullptr)
        return 0;

    int count = 0;

    for (int i = 0; i < hdr.numRecordsPerPage; i++){
        if (IsSlotOccupied(pData, i)) count++;
    }

    return count;
}

bool RM_FileHandle::PageHasFreeSlot(const char *pData) const {
    if (pData == nullptr)
        return false;

    RM_PageHdr *pHdr = (RM_PageHdr *)pData;

    return pHdr->numRecords < hdr.numRecordsPerPage;
    
}


// Remove a page from the free-page doubly-linked list
RC RM_FileHandle::RemoveFromFreeList(PageNum pageNum) {
    if (!bOpen)
        return RM_INVALIDFILE;
    

    if (pageNum < 1 || pageNum > hdr.numPages)
        return RM_INVALIDRID;

    RC rc;

    // Read the page header
    PF_PageHandle ph;
    if ((rc = pfHandle.GetThisPage(pageNum, ph))) return rc;

    char *pData;
    if ((rc = ph.GetData(pData))) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    RM_PageHdr *pgh = reinterpret_cast<RM_PageHdr*>(pData);

    PageNum prev = pgh->prevFreePage;
    PageNum next = pgh->nextFreePage;

    /*
     * A page being removed must either:
     *
     *   1. be the free-list head, or
     *   2. have a valid previous free page.
     *
     * If neither is true, the list metadata is inconsistent.
     */
    if (prev == -1 && hdr.firstFreePage != pageNum) {
        pfHandle.UnpinPage(pageNum);
        return RM_PAGECORRUPT;
    }

    /*
     * Update the previous page.
     */
    if (prev != -1) {
        PF_PageHandle prevPH;

        if ((rc = pfHandle.GetThisPage(prev, prevPH))) {
            pfHandle.UnpinPage(pageNum);
            return rc;
        }

        char *prevData = nullptr;

        if ((rc = prevPH.GetData(prevData))) {
            pfHandle.UnpinPage(prev);
            pfHandle.UnpinPage(pageNum);
            return rc;
        }

        RM_PageHdr *prevHdr =
            reinterpret_cast<RM_PageHdr *>(prevData);

        prevHdr->nextFreePage = next;

        if ((rc = pfHandle.MarkDirty(prev))) {
            pfHandle.UnpinPage(prev);
            pfHandle.UnpinPage(pageNum);
            return rc;
        }

        if ((rc = pfHandle.UnpinPage(prev))) {
            pfHandle.UnpinPage(pageNum);
            return rc;
        }
    }
    else {
        /*
         * pageNum was the head of the free list.
         */
        hdr.firstFreePage = next;
        bHdrModified = true;
    }

    /*
     * Update the next page.
     */
    if (next != -1) {
        PF_PageHandle nextPH;

        if ((rc = pfHandle.GetThisPage(next, nextPH))) {
            pfHandle.UnpinPage(pageNum);
            return rc;
        }

        char *nextData = nullptr;

        if ((rc = nextPH.GetData(nextData))) {
            pfHandle.UnpinPage(next);
            pfHandle.UnpinPage(pageNum);
            return rc;
        }

        RM_PageHdr *nextHdr =
            reinterpret_cast<RM_PageHdr *>(nextData);

        nextHdr->prevFreePage = prev;

        if ((rc = pfHandle.MarkDirty(next))) {
            pfHandle.UnpinPage(next);
            pfHandle.UnpinPage(pageNum);
            return rc;
        }

        if ((rc = pfHandle.UnpinPage(next))) {
            pfHandle.UnpinPage(pageNum);
            return rc;
        }
    }

    /*
     * Disconnect the page itself from the list.
     */
    pgh->prevFreePage = -1;
    pgh->nextFreePage = -1;

    
    if ((rc = pfHandle.MarkDirty(pageNum))) {
            pfHandle.UnpinPage(pageNum);
            return rc;
    }

    if ((rc = pfHandle.UnpinPage(pageNum)))
        return rc;


    return 0;
}

// Insert a page at the front of the free-page list
void RM_FileHandle::AddToFreeList(PageNum pageNum)
{
    if (!bOpen)
        return;

    if (pageNum < 1 || pageNum > hdr.numPages)
        return;

    RC rc;

    PF_PageHandle ph;

    if ((rc = pfHandle.GetThisPage(pageNum, ph)))
        return;

    char *pData = nullptr;

    if ((rc = ph.GetData(pData))) {
        pfHandle.UnpinPage(pageNum);
        return;
    }

    RM_PageHdr *pageHdr =
        reinterpret_cast<RM_PageHdr *>(pData);

    PageNum oldHead = hdr.firstFreePage;

    /*
     * Insert page at the front of the free list.
     *
     *       old list:
     *
     *       HEAD -> A <-> B
     *
     *       becomes:
     *
     *       HEAD -> pageNum <-> A <-> B
     */
    pageHdr->prevFreePage = -1;
    pageHdr->nextFreePage = oldHead;

    if ((rc = pfHandle.MarkDirty(pageNum))) {
        pfHandle.UnpinPage(pageNum);
        return;
    }

    if ((rc = pfHandle.UnpinPage(pageNum)))
        return;

    /*
     * Update the old head's previous pointer.
     */
    if (oldHead != -1) {
        PF_PageHandle headPH;

        if ((rc = pfHandle.GetThisPage(oldHead, headPH)))
            return;

        char *headData = nullptr;

        if ((rc = headPH.GetData(headData))) {
            pfHandle.UnpinPage(oldHead);
            return;
        }

        RM_PageHdr *headHdr =
            reinterpret_cast<RM_PageHdr *>(headData);

        headHdr->prevFreePage = pageNum;

        if ((rc = pfHandle.MarkDirty(oldHead))) {
            pfHandle.UnpinPage(oldHead);
            return;
        }

        if ((rc = pfHandle.UnpinPage(oldHead)))
            return;
    }

    /*
     * Finally update the cached RM file header.
     */
    hdr.firstFreePage = pageNum;
    bHdrModified = true;
}
 

// Find an existing page with a free slot, or allocate a new one.
RC RM_FileHandle::FindOrAllocatePage(PageNum &pageNum, PF_PageHandle &ph) {
    if (!bOpen)
        return RM_INVALIDFILE;

    RC rc;
    
    // Existing free page
    if (hdr.firstFreePage != -1) {
        // Use the head of the free list
        pageNum = hdr.firstFreePage;
        if ((rc = pfHandle.GetThisPage(pageNum, ph))) return rc;
        return 0;
    }
 
    // No free page — allocate a new data page
    if ((rc = pfHandle.AllocatePage(ph)) ) return rc;

    char *pData;

    if ((rc = ph.GetData(pData)) ) {
        PageNum allocatedPage;

        if (ph.GetPageNum(allocatedPage) == 0)
            pfHandle.UnpinPage(allocatedPage);

        return rc;
    }

    if ((rc = ph.GetPageNum(pageNum)) ) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }
 
    // Initialise the new page header
    RM_PageHdr *pgh = reinterpret_cast<RM_PageHdr*>(pData);

    pgh->numRecords  = 0;
    pgh->nextFreePage = -1;
    pgh->prevFreePage = -1;

    // clear bitmap
    memset(BitmapPtr(pData), 0, hdr.bitmapSize);

    // The newly allocated page is dirty
    pfHandle.MarkDirty(pageNum);
    
    // file header value changes
    hdr.numPages = pageNum;
    bHdrModified = true;
 
    // Keep pinned for caller; 
    // AddToFreeList will unpin via GetThisPage internally.
    // We need to unpin first so AddToFreeList can re-pin.
    
    pfHandle.UnpinPage(pageNum);
 
    // Add new page to free list then re-pin for the caller
    AddToFreeList(pageNum);
    if ((rc = pfHandle.GetThisPage(pageNum, ph)) ) return rc;
 
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

    if ((rc = rid.GetPageNum(pageNum)) ) return RM_INVALIDRID;
    if ((rc = rid.GetSlotNum(slotNum)) ) return RM_INVALIDRID;

    // page 0 is the header page; data pages start at 1
    if (pageNum < 1 || pageNum > hdr.numPages) return RM_INVALIDRID;

    
    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage) return RM_INVALIDRID;

    PF_PageHandle ph;

    if ((rc = pfHandle.GetThisPage(pageNum, ph))) return rc;

    char *pPageData;
    if (( rc = ph.GetData(pPageData))){
        // pfHandle.UnpinPage(pageNum);
        return rc;
    };

     if (!IsSlotOccupied(pPageData, slotNum)) {
        pfHandle.UnpinPage(pageNum);
        return RM_RECORDNOTFOUND;
    }
 
    char *pSlotData;
    if ((rc = GetSlotPtr(pPageData, slotNum, pSlotData))) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }
 
    rec.Set(pSlotData, hdr.recordSize, rid);

    if ((rc = pfHandle.UnpinPage(pageNum)))
        return rc;

    return 0;
}

RC RM_FileHandle::GetPageData(
    PageNum pageNum,
    char *&pData,
    PF_PageHandle &ph
) const
{
    if (!bOpen)
        return RM_INVALIDFILE;

    if (pageNum < 1 || pageNum > hdr.numPages)
        return RM_INVALIDRID;

    RC rc;

    if ((rc = pfHandle.GetThisPage(pageNum, ph)) )
        return rc;

    if ((rc = ph.GetData(pData)) ) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    return 0;
}

// Design: validate data -> Find page -> Find free slot -> Find slot addr ->
// copy rec -> set bitmap[slot]=1 -> numRecords++ ->
// is page full? yes-> rm frm free list, no-> continue
RC RM_FileHandle::InsertRec(const char *pData, RID &rid)
{
    if (!bOpen)
        return RM_INVALIDFILE;

    if (pData == nullptr)
        return RM_INVALIDRECORD;

    if (hdr.recordSize <= 0)
        return RM_INVALIDRECORDSIZE;

    if (hdr.numRecordsPerPage <= 0)
        return RM_PAGECORRUPT;

    RC rc;

    PageNum pageNum;
    SlotNum slotNum;

    PF_PageHandle ph;

    /*
     * Find an existing page with free space or allocate a new one.
     *
     * FindOrAllocatePage() returns the page pinned.
     */
    if ((rc = FindOrAllocatePage(pageNum, ph)))
        return rc;

    /*
     * Get the actual data buffer belonging to the PF page.
     *
     * IMPORTANT:
     *
     * pData is the caller's record.
     * pPageData is the RM page.
     */
    char *pPageData = nullptr;

    if ((rc = ph.GetData(pPageData))) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    /*
     * Find an unused slot.
     */
    if ((rc = FindFreeSlot(pPageData, slotNum))) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    /*
     * Find the physical address of the slot.
     */
    char *pSlotData = nullptr;

    if ((rc = GetSlotPtr(
        pPageData,
        slotNum,
        pSlotData
    ))) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    /*
     * Get the page header.
     */
    RM_PageHdr *pageHdr =
        reinterpret_cast<RM_PageHdr *>(pPageData);

    /*
     * The page should have at least one free slot because
     * FindFreeSlot() succeeded.
     *
     * Determine whether this insertion will make the page full.
     */
    bool becomesFull =
        (pageHdr->numRecords ==
         hdr.numRecordsPerPage - 1);

    /*
     * Copy the caller's record into the page slot.
     */
    std::memcpy(
        pSlotData,
        pData,
        hdr.recordSize
    );

    /*
     * Mark the slot occupied.
     */
    SetSlotOccupied(
        pPageData,
        slotNum,
        true
    );

    /*
     * Update page record count.
     */
    ++pageHdr->numRecords;

    /*
     * The page has changed.
     */
    if ((rc = pfHandle.MarkDirty(pageNum))) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    /*
     * Construct the RID for the newly inserted record.
     */
    rid = RID(pageNum, slotNum);

    /*
     * We are finished modifying the page.
     */
    if ((rc = pfHandle.UnpinPage(pageNum)))
        return rc;

    /*
     * If this insertion consumed the last free slot,
     * the page must leave the free-page list.
     */
    if (becomesFull) {
        RemoveFromFreeList(pageNum);
    }

    /*
     * RemoveFromFreeList() may have modified the RM header.
     * FindOrAllocatePage()/AddToFreeList() may also have done so.
     */
    if (bHdrModified) {
        if ((rc = WriteHdr()))
            return rc;
    }

    return 0;
}

// Validate Rid -> get page -> check bitmap ->
// set bitmap[slot]=0 -> numRecords-- ->
// was page full? yes-> add to freelist, no=>continue
RC RM_FileHandle::DeleteRec(const RID &rid)
{
    if (!bOpen)
        return RM_INVALIDFILE;

    RC rc;

    PageNum pageNum;
    SlotNum slotNum;

    /*
     * Extract RID components.
     */
    if ((rc = rid.GetPageNum(pageNum)))
        return RM_INVALIDRID;

    if ((rc = rid.GetSlotNum(slotNum)))
        return RM_INVALIDRID;

    /*
     * Validate page.
     *
     * Page 0 is the RM file header.
     */
    if (pageNum < 1 || pageNum > hdr.numPages)
        return RM_INVALIDRID;

    /*
     * Validate slot.
     */
    if (slotNum < 0 ||
        slotNum >= hdr.numRecordsPerPage)
        return RM_INVALIDRID;

    /*
     * Fetch the data page.
     */
    PF_PageHandle ph;

    if ((rc = pfHandle.GetThisPage(pageNum, ph)))
        return rc;

    char *pPageData = nullptr;

    if ((rc = ph.GetData(pPageData))) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    /*
     * Verify that the slot actually contains a record.
     */
    if (!IsSlotOccupied(pPageData, slotNum)) {
        pfHandle.UnpinPage(pageNum);
        return RM_RECORDNOTFOUND;
    }

    RM_PageHdr *pageHdr =
        reinterpret_cast<RM_PageHdr *>(pPageData);

    /*
     * IMPORTANT:
     *
     * Determine whether the page was FULL before deletion.
     *
     * A page that was already partially free is already on the
     * free list, so it must NOT be inserted again.
     */
    bool wasFull =
        (pageHdr->numRecords ==
         hdr.numRecordsPerPage);

    /*
     * Clear the bitmap bit.
     *
     * Bit = 0 means the slot is now free.
     */
    SetSlotOccupied(
        pPageData,
        slotNum,
        false
    );

    /*
     * The page header should never claim that a page has
     * zero records before this deletion.
     */
    if (pageHdr->numRecords <= 0) {
        pfHandle.UnpinPage(pageNum);
        return RM_PAGECORRUPT;
    }

    --pageHdr->numRecords;

    /*
     * Mark the data page dirty.
     */
    if ((rc = pfHandle.MarkDirty(pageNum))) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    /*
     * We are finished modifying the page.
     */
    if ((rc = pfHandle.UnpinPage(pageNum)))
        return rc;

    /*
     * Only a FULL -> FREE transition requires modifying
     * the free-page list.
     *
     * If the page was already partially free, it is already
     * somewhere in the free list.
     */
    if (wasFull) {
        AddToFreeList(pageNum);

        /*
         * AddToFreeList() updates the cached RM file header.
         */
        if (bHdrModified) {
            if ((rc = WriteHdr()))
                return rc;
        }
    }

    return 0;
}


RC RM_FileHandle::UpdateRec(const RM_Record &rec){
    if (!bOpen)
        return RM_INVALIDFILE;

    if (!rec.valid)
        return RM_INVALIDRECORD;

    if (rec.pData == nullptr)
        return RM_INVALIDRECORD;

    if (rec.dataSize != hdr.recordSize)
        return RM_INVALIDRECORD;


    PageNum pageNum;
    SlotNum slotNum;

    RC rc;

    // Get RID from RM_Record
    if ((rc = rec.rid.GetPageNum(pageNum)) )
        return RM_INVALIDRID;

    if ((rc = rec.rid.GetSlotNum(slotNum)) )
        return RM_INVALIDRID;
    

    // Validate RID
    if (pageNum < 1 || pageNum > hdr.numPages)
        return RM_INVALIDRID;
    
    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage)
        return RM_INVALIDRID;

    // Fetch page
    PF_PageHandle ph;

    if ((rc = pfHandle.GetThisPage(pageNum, ph)) )
        return rc;

    char *pPageData;

    if ((rc = ph.GetData(pPageData))){
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    // Target slot must already contain a record
    if (!IsSlotOccupied(pPageData, slotNum)) {
        pfHandle.UnpinPage(pageNum);
        return RM_RECORDNOTFOUND;
    }

    // Find the target slot.
    char *pSlotData;

    if ((rc = GetSlotPtr(pPageData, slotNum, pSlotData))){
        pfHandle.UnpinPage(pageNum);
        return rc;
    }
        
    // Replace existing rec contents
    // The RID and slot don't change

    memcpy(pSlotData, rec.pData, hdr.recordSize);

    // Mark page dirty and unpin it;
    pfHandle.MarkDirty(pageNum);
    pfHandle.UnpinPage(pageNum);

    return 0;


}


RC RM_FileHandle::ForcePages(PageNum pageNum){
    if (!bOpen)
        return RM_INVALIDFILE;

    RC rc;

    // Header is maintained separately from PF dirty pages.
    // Make sure the cached RM header is written first.
    if(bHdrModified){
        if ((rc = WriteHdr()) ){
            return rc;
        }
    }

    // Force requested PF pages
    pfHandle.ForcePages(pageNum);

    return 0;


}

RC RM_FileHandle::ReadHdr(){ 
    //getfirstpage (page 0) -> pinpage -> copy contents to filehandle -> unpin and maybe close
    PF_PageHandle ph;
    RC rc;

    if ((rc = pfHandle.GetThisPage(0, ph))) return rc; // we could use GetThisPage(0, ph) but mmhe

    char *pData;
   
    if ((rc = ph.GetData(pData)) ){
        return rc;
    } // set pData 

    memcpy(&hdr, pData, sizeof(RM_FileHdr));

    pfHandle.UnpinPage(0);

    bHdrModified = false;

    return 0;

}

// Write the cached file header back to page 0
RC RM_FileHandle::WriteHdr(){
     if (!bOpen)
        return RM_INVALIDFILE;

    PF_PageHandle ph;
    RC rc;

    if ((rc = pfHandle.GetThisPage(0, ph)) )
        return rc;

    char *pData;

    if ((rc = ph.GetData(pData)) ) {
        pfHandle.UnpinPage(0);
        return rc;
    }

    memcpy(pData, &hdr, sizeof(RM_FileHdr));

    if ((rc = pfHandle.MarkDirty(0)) ) {
        pfHandle.UnpinPage(0);
        return rc;
    }

    if ((rc = pfHandle.UnpinPage(0)) )
        return rc;

    bHdrModified = false;

    return 0;
}