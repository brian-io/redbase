#include "rm.h"
#include <math.h>
#include <cstring>

RM_Manager::RM_Manager(PF_Manager &pfm)
: pfm(pfm)
{
}

RM_Manager::~RM_Manager(){
    //
}


/*
* Create a new record file given its filename and filehandle.
* It initializes the file header page (0)
*/

RC RM_Manager::CreateFile(const char *fileName, int recordSize){


    if(!fileName)
        // return some error code
        return RM_NULLPOINTER;
    

    if (recordSize <= 0 || recordSize > PF_PAGE_SIZE - (int)sizeof(RM_FileHandle::RM_PageHdr) - 1)
        return RM_INVALID_RECSIZE;

    RC rc;

    // create paged file
    if ((rc = pfm.CreateFile(fileName)))
        return rc;
    
    PF_FileHandle fh;

    // Open the file to write to the page header
    if ((rc = pfm.OpenFile(fileName, fh)))
        return rc;
    
    PF_PageHandle ph;

    // Allocate the header page (page 0)
    if ((rc = fh.AllocatePage(ph)))
        pfm.CloseFile(fh);
        return rc;

    char *pData;

    // set ph->pData to initial values. That is, initialize the header page
    if ((rc = ph.GetData(pData))){
        // if there's data present then unpin and close the page
        fh.UnpinPage(0);
        pfm.CloseFile(fh);
        return rc;
    }

    // set current page number; should be 0 for the first allocated page
    PageNum pageNum;
    if((rc = ph.GetPageNum(pageNum))) {
        // if present unpin and close page
        fh.UnpinPage(0);
        pfm.CloseFile(fh);
        return rc;
    }

    // Fill the header structure
    RM_FileHandle::RM_FileHdr hdr;
    hdr.recordSize = recordSize;

    int maxSlots, bitmapSize, dataOffset;
    ComputePageLayout(recordSize, maxSlots, bitmapSize, dataOffset);
    hdr.numRecordsPerPage = maxSlots;
    hdr.bitmapSize = bitmapSize;
    hdr.pageDataOffset = dataOffset;
    hdr.numPages = 1;   // only header page so far;
    hdr.firstFreePage = -1; // no data pages yet

    memcpy(pData, &hdr, sizeof(hdr));  // set the pagehanldes's pData to the file contents of page 0

    if ((rc = fh.MarkDirty(pageNum))) { 
        fh.UnpinPage(pageNum); 
        pfm.CloseFile(fh); 
        return rc; 
    }
    if ((rc = fh.UnpinPage(pageNum))) { 
        pfm.CloseFile(fh); 
        return rc; }

    if ((rc = pfm.CloseFile(fh))) 
        return rc;


    return(0) ;

}


/*
*
*
*/
RC RM_Manager::OpenFile(const char *fileName, RM_FileHandle &fileHandle){
    
    if (!fileName)
        return RM_NULLPOINTER;

    if (fileHandle.bOpen)
        return RM_INVALID_FILEHANDLE; // already open and is associated with another file

    RC rc;
    
    // Open the PF file
    if ((rc = pfm.OpenFile(fileName, fileHandle.pfHandle)))
        return rc;

    // Read the file header from page 0
    if((rc = fileHandle.ReadHdr())){
        pfm.CloseFile(fileHandle.pfHandle);
        return rc;
    }
    
    
    fileHandle.bOpen = true;
    fileHandle.bHdrModified = false;

    return 0;

    
}


/*
*
*
*/
RC RM_Manager::CloseFile(RM_FileHandle &fileHandle){
    
    if(!fileHandle.bOpen)
        return RM_INVALID_FILEHANDLE;

    RC rc;
    
    if(fileHandle.bHdrModified){
        if((rc = fileHandle.WriteHdr()))
            return rc;

        fileHandle.bHdrModified = false;
    }

    if ((rc = pfm.CloseFile(fileHandle.pfHandle))) return rc;

    fileHandle.bOpen = false;

    return (0);

}

/*
*
*
*/

RC RM_Manager::DestroyFile(const char *fileName){
     if (!fileName)
        return RM_NULLPOINTER;

    return pfm.DestroyFile(fileName);

}


/*
* Computes the number of records that fit per page
* Each PageLayout: { [RM_PageHdr] [bitmap of bitmapSize bytes] [record1 | record2 | record3 | ...] }
* We want to pack as many records as possible.
* Let n = numRecordsPerPage
* We need: sizeof(RM_PageHdr) + ceil(n/8) + n*recordSize <= PF_PAGE_SIZE
* We solve iteratively
*/

static void ComputePageLayout(int recordSize, int &numRecordsPerPage,
                                int &bitmapSize, int &pageDataOffset)

{
    int availableSpace = PF_PAGE_SIZE;
    int maxSlots = (availableSpace - (int)sizeof(RM_FileHandle::RM_PageHdr) * 8) / (recordSize * 8 + 1);

    if (maxSlots < 1)
        maxSlots = 1;
     
    int bitmapSize = static_cast<int>(std::ceil(maxSlots / 8.0));

    // we iterate because of the circular dependence
    // between bitmapSize and the number of records to be stored

    while(true) {
        int _bitmapSize = (maxSlots + 7) / 8; // from the definition that ceil[a/b]= (a + b - 1)/b
        int totalSize = (int)sizeof(RM_FileHandle::RM_PageHdr) + _bitmapSize + maxSlots * recordSize;
        if (totalSize <= PF_PAGE_SIZE) break;
        maxSlots--;
        if (maxSlots <= 0 ){
            maxSlots = 0;
            break;
        }
    }

    numRecordsPerPage = maxSlots;
    bitmapSize = (maxSlots + 7) / 8;
    pageDataOffset = (int)sizeof(RM_FileHandle::RM_PageHdr) + bitmapSize;
}   