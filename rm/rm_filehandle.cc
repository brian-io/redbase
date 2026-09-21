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

    return (bm[SlotNum / 8] & (1u << (SlotNum % 8))) != 0;

}

// Set or clear the bit slotNum in the bitmap
void RM_FileHandle::SetSlotOccupied(char *pData, SlotNum slotNum, bool occupied)  {
     
    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage)
        return RM_INVALIDRID;

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
    RC rc;
    // Read the page header
    PF_PageHandle ph;
    if (pfHandle.GetThisPage(pageNum, ph) != 0) return rc;

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
            if (nextPH.GetData(nextData) == 0) {

                RM_PageHdr *nextHdr =
                    reinterpret_cast<RM_PageHdr *>(nextData);

                nextHdr->prevFreePage = prev;

                pfHandle.MarkDirty(next);
            }
            pfHandle.UnpinPage(next);
        }
    }

    return 0;
}

// Insert a page at the front of the free-page list
void RM_FileHandle::AddToFreeList(PageNum pageNum) {
  
    PF_PageHandle ph;

    if (pfHandle.GetThisPage(pageNum, ph) != 0) return;

    char *pData;

    if (ph.GetData(pData) != 0) {
        pfHandle.UnpinPage(pageNum);
        return;
    }

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
            if (headPH.GetData(headData) == 0) {

                RM_PageHdr *headHdr =
                    reinterpret_cast<RM_PageHdr *>(headData);

                headHdr->prevFreePage = pageNum;

                pfHandle.MarkDirty(oldHead);
            }
            pfHandle.UnpinPage(oldHead);
        }
    }
 
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
    if ((rc = pfHandle.AllocatePage(ph)) != 0) return rc;

    char *pData;

    if ((rc = ph.GetData(pData)) != 0) {
        PageNum allocatedPage;

        if (ph.GetPageNum(allocatedPage) == 0)
            pfHandle.UnpinPage(allocatedPage);

        return rc;
    }

    if ((rc = ph.GetPageNum(pageNum)) != 0) {
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
    hdr.numPages++;
    bHdrModified = true;
 
    // Keep pinned for caller; 
    // AddToFreeList will unpin via GetThisPage internally.
    // We need to unpin first so AddToFreeList can re-pin.
    
    pfHandle.UnpinPage(pageNum);
 
    // Add new page to free list then re-pin for the caller
    AddToFreeList(pageNum);
    if ((rc = pfHandle.GetThisPage(pageNum, ph)) != 0) return rc;
 
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

    if ((rc = rid.GetPageNum(pageNum)) != 0) return RM_INVALIDRID;
    if ((rc = rid.GetSlotNum(slotNum)) != 0) return RM_INVALIDRID;

    // page 0 is the header page; data pages start at 1
    if (pageNum < 1 || pageNum > hdr.numPages) return RM_INVALIDRID;

    
    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage) return RM_INVALIDRID;

    PF_PageHandle ph;

    if (( rc = pfHandle.GetThisPage(pageNum, ph))) return rc;

    char *pData;
    if (( rc = ph.GetData(pData)) != 0){
        pfHandle.UnpinPage(pageNum);
        return rc;
    };

     if (!IsSlotOccupied(pData, slotNum)) {
        pfHandle.UnpinPage(pageNum);
        return RM_RECORDNOTFOUND;
    }
 
    char *pSlot;
    if ((rc = GetSlotPtr(pData, slotNum, pSlot)) != 0) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }
 
    rec.Set(pSlot, hdr.recordSize, rid);

    pfHandle.UnpinPage(pageNum);
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

    if ((rc = pfHandle.GetThisPage(pageNum, ph)) != 0)
        return rc;

    if ((rc = ph.GetData(pData)) != 0) {
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    return 0;
}

// Design: validate data -> Find page -> Find free slot -> Find slot addr ->
// copy rec -> set bitmap[slot]=1 -> numRecords++ ->
// is page full? yes-> rm frm free list, no-> continue
RC RM_FileHandle::InsertRec(const char *pData, RID &rid ){
    if (!bOpen){
        return RM_INVALIDFILE;
    }

    if (pData == nullptr){
        return RM_INVALIDRECORD;
    }

    if (hdr.recordSize){
        return RM_INVALIDRECORDSIZE;
    }

    RC rc;

    PageNum pageNum;
    SlotNum slotNum;

    // find a page with free space, or allocate one.
    // returned page is pinned therefore must unpin before moving on
    if ((rc = FindOrAllocatePage(pageNum, ph)) != 0)
        return rc;

    char *pData;

    if ((rc = GetData(pData)) != 0){
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    // find free slot
    if ((rc = FindFreeSlot(pData, slotNum)) != 0){
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    // locate slot
    char *pSlotData;
    if ((rc = GetSlotPtr(pData, slotNum, pSlotData)) != 0){
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    // Copy record into page
    memcpy(pSlotData, pData, hdr.recordSize);

    // Mark slot occupied.
    SetSlotOccupied(pData, slotNum, true);

    RM_PageHdr *pgh = reinterpret_cast<RM_PageHdr*>(pData);

    // Track transition to full.
    // If the page had [capacity-1] records before insertion,
    // this insertion makes it full

    bool becomesFull = (pgh->numRecords == numRecordsPerPage - 1);
    ++pgh->numRecords;

    // Page has changed
    pfHandle.MarkDirty(pageNum);

    // Build RID while page is still valid
    rid = RID(pageNum, slotNum);

    // Unpin before modifying the free list
    if ((rc = pfHandle.UnpinPage(pageNum)) != 0)
        return rc;

    // Page becomes full. It must no longer be in the free-page list
    if(becomesFull){
        RemoveFromFreeList(pageNum);
    }

    // Persist modified file header when necessary
    if(bHdrModified){
        if((rc = WriteHdr()) != 0)
            return rc;
    }

    return 0;


}

// Validate Rid -> get page -> check bitmap ->
// set bitmap[slot]=0 -> numRecords-- ->
// was page full? yes-> add to freelist, no=>continue
RC RM_FileHandle::DeleteRec(const RID &rid){
    if(!bOpen){
        return RM_INVALIDFILE;
    }
    Rc rc;
    PageNum pageNum;
    SlotNum slotNum;

    // Validate RID
    if ((rc = rid.GetPageNum(pageNum)) != 0){
        return RM_INVALIDRID;
    }

    if ((rc = rid.GetSlotNum(slotNum)) != 0){
        return RM_INVALIDRID;
    }

    if (pageNum < 1 || pageNum > hdr.numPages){
        return RM_INVALIDRID;

    }

    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage){
        return RM_INVALIDRID;

    }

    // Fetch page
    PF_PageHandle ph;

    if ((rc = pfHandle.GetThisPage(pageNum, ph)) != 0){
        return rc;
    }

    char *pData;

    if ((rc = ph.GetData(pData)) != 0){
        pfHandle.UnpinPage(pageNum);
        return rc;
    }

    // Check if slot is occupied
    if (!IsSlotOccupied(pData, slotNum)){
        pfHandle.UnpinPage(pageNum);
        return RM_RECORDNOTFOUND;
    }

    // Determine whether page was full before deletion
    // Only a full page needs to be inserted into the freelist

    RM_PageHdr *pgh = reinterpret_cast<RM_PageHdr*>(pData);

    bool wasFull = (pgh->numRecords == hdr.numRecordsPerPage);

    // Clear bitmap bit. This is the deletion of the record
    // It will be cleared out by PF_Manager**(check this later)
    SetSlotOccupied(pData, slotNum, false);

    // Update record count
    if (pgh->numRecords <= 0){
        pfHandle.UnpinPage(pageNum);
        return RM_PAGECORRUPT;
    }

    --pgh->numRecords

    // Mark page dirty
    pfHandle.MarkDirty(pageNum);

    // Unpin page before manipulating freelist
    if((rc = pfHandle.UnpinPage(pageNum)) != 0){
        return rc;
    }

    // Fule page now has free slot. Add it to the front of
    // the freelist
    if (wasFull){
        AddToFreeList(pageNum);
    }

    // File header normally doesn't change on deletion unless the 
    // freelist head has changed
    if (bHdrModified){
        if((rc = WriteHdr()) != 0){
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
    if ((rc = rec.rid.GetPageNum(pageNum)) != 0)
        return RM_INVALIDRID;

    if ((rc = rec.rid.GetSlotNum(slotNum)) != 0)
        return RM_INVALIDRID;
    

    // Validate RID
    if (pageNum < 1 || pageNum > hdr.numPages)
        return RM_INVALIDRID;
    
    if (slotNum < 0 || slotNum >= hdr.numRecordsPerPage)
        return RM_INVALIDRID;

    // Fetch page
    PF_PageHandle ph;

    if ((rc = pfHandle.GetThisPage(pageNum, ph)) != 0)
        return rc;

    char *pPageData;

    if ((rc = GetData(pPageData)) != 0)
        pfHandle.UnpinPage(pageNum);
        return rc;

    // Target slot must already contain a record
    if (!IsSlotOccupied(pPageData, slotNum)) {
        pfHandle.UnpinPage(pageNum);
        return RM_RECORDNOTFOUND;
    }

    // Find the target slot.
    char *pSlotData;

    if ((rc = GetSlotPtr(pPageData, slotNum, pSlotData)) != 0)
        pfHandle.UnpinPage(pageNum);
        return rc;

    // Replace existing rec contents
    // The RID and slot don't change

    memcpy(pSlotData, rec.pData, hdr.recordSize);

    // Mark page dirty and unpin it;
    pfHandle.MarkDirty(pageNum);
    pfHandle.UnpinPage(pageNum);

    return 0;


}


RC RM_FileHandle::ForcePages(PageNum PageNum = ALL_PAGES){
    if (!bOpen)
        return RM_INVALIDFILE;

    RC rc;

    // Header is maintained separately from PF dirty pages.
    // Make sure the cached RM header is written first.
    if(bHdrModified){
        if ((rc = WriteHdr()) != 0){
            return rc;
        }
    }

    // Force requested PF pages
    pfHandle.ForcePages(pageNum);

    return 0;


}

RC RM_FileHandle::ReadHdr(){ 
    // open file but we expect it to already be opened
    if(!bOpen)
        return RM_INVALIDFILE;

    //getfirstpage (page 0) -> pinpage -> copy contents to filehandle -> unpin and maybe close
    PF_PageHandle ph;
    RC rc;

    if ((rc = pfHandle.GetThisPage(0, ph))) return rc; // we could use GetThisPage(0, ph) but mmhe

    char *pData;
   
    if ((rc = ph.GetData(pData)) != 0){
        pfHandle.UnpinPage(ph.GetPageNum);
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

    if ((rc = pfHandle.GetThisPage(0, ph)) != 0)
        return rc;

    char *pData;

    if ((rc = ph.GetData(pData)) != 0) {
        pfHandle.UnpinPage(0);
        return rc;
    }

    memcpy(pData, &hdr, sizeof(RM_FileHdr));

    if ((rc = pfHandle.MarkDirty(0)) != 0) {
        pfHandle.UnpinPage(0);
        return rc;
    }

    if ((rc = pfHandle.UnpinPage(0)) != 0)
        return rc;

    bHdrModified = false;

    return 0;
}