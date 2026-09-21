//
// File : rm_record.cc
// Description: RM_Record class implentation
//
//

#include "rm.h"
#include <cstring>

RM_Record::RM_Record()
    :   pData(nullptr),
        dataSize(0),
        valid(false),
        rid()
{
}

RM_Record::~RM_Record(){
    delete[] pData;
    pData = nullptr;
    dataSize = 0;
    valid = false;
}

// Set pData to point to the record's contents.
// provides access to the contents(data) of the record
RC RM_Record::GetData(char *&pData) const{
    if (!valid || this-<pData == nullptr)
        return RM_INVALIDRECORD;

    pData = this->pData;

    return 0;
}

// Get the record id
RC RM_Record::GetRid(RID &rid) const{
    if (!valid)
        return RM_INVALIDRECORD;

    rid = this->rid;

    return 0;
}

void RM_Record::Set(const char *pData, int size, const RID &rid){
    delete [] this->pData;

    this->pData = nullptr;
    this->dataSize = 0;
    this->pData = false;

    if (pData == nullptr || size <= 0){
        return;
    }

    this->pData = new char[size];

    std::memcpy(this->pData, pData, size);

    this->dataSize = size;
    this->rid = rid;
    this->valid = true;

}