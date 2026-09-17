// File: rm_filescan.cc
// Description: Implements the RM_FileScan class.
// This class provides clients the capability to perform
// scans over the records of an RM component file
// where a scan may be based on a specified condition.


#include "rm.h"


RM_FileScan::RM_FileScan():{

}

RM_FileScan::~RM_FileScan(){
    
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

}                         

// Get next matching record
RC RM_FileScan::GetNextRec(RM_Record &rec){

}

RC RM_FileScan::Closescan(){

}