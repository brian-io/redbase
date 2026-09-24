//
// rm_rid.cc
//


#include "rm_rid.h"

// Default constructor
RID::RID() : pageNum(-1), slotNum(-1), valid(false){
    //
}

RID::~RID(){
    //
}

// Parameterized constructor
RID::RID(PageNum pageNum, SlotNum slotNum) 
: pageNum(pageNum), slotNum(slotNum), valid(true){
    //
}

// Copy constructor
RID::RID(const RID& rid) 
: pageNum(rid.pageNum), slotNum(rid.slotNum), valid(rid.valid){
    //
}

// Assignment operator
RID& RID::operator=(const RID &rid) {
    if (this != &rid) {
        pageNum  = rid.pageNum;
        slotNum  = rid.slotNum;
        valid = rid.valid;
    }
    return *this;
}

// Return the page number of a record
RC RID::GetPageNum(PageNum &page) const {
    if (!valid) 
        return RM_INVALIDRID;

    page = pageNum;
    return 0;
}

// Return the slot number of a record in a page
RC RID::GetSlotNum(SlotNum &slot) const {
    if (!valid) 
        return RM_INVALIDRID;

    slot = slotNum;
    return 0;
}

bool RID::IsValid() const {
    return valid;
}
