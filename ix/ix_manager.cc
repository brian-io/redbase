//
// File:    ix_manager.cc
// Description: IX_Manager class implementation
//

#include "ix_internal.h"


// Constructor
IX_Manager::IX_Manager(PF_Manager &pfm)
{

}

// Destructor
IX_Manager::~IX_Manager()
{
  
}

RC IX_Manager::CreateIndex(
  const char *fileName,          // Create new index
  int        indexNo,
  AttrType   attrType,
  int        attrLength){
    

    if (fileName == nullptr)
      return IX_INVALIDFILE;
  
}

RC IX_Manager::DestroyIndex(
  const char *fileName,
  int        indexNo
){
  
}

RC IX_Manager::OpenIndex(
  const char *fileName,         
  int        indexNo,
  IX_IndexHandle &indexHandle
){
  
}

RC IX_Manager::CloseIndex(IX_IndexHandle &fileName){
  
}