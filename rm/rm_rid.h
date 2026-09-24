//
// rm_rid.h
//
//   The Record Id interface
//

#ifndef RM_RID_H
#define RM_RID_H

// We separate the interface of RID from the rest of RM because some
// components will require the use of RID but not the rest of RM.

#include "../redbase.h"

//
// PageNum: uniquely identifies a page in a file
//
typedef int PageNum;

//
// SlotNum: uniquely identifies a record in a page
//
typedef int SlotNum;


// RID return codes
#define RM_INVALIDRID      2                       // RID is not valid (not yet set)

//
// RID: Record id interface
//
class RID {
public:
    RID();                                         // Default constructor
    ~RID();                                        // Destructor

    RID(PageNum pageNum, SlotNum slotNum);         // Construct from page+slot

    RID(const RID& rid);                           // Copy constructor

    RID& operator=(const RID& rid);

    RC GetPageNum(PageNum &pageNum) const;         // Return page number
    RC GetSlotNum(SlotNum &slotNum) const;         // Return slot number

    inline bool operator==(const RID& r) const {
        return valid && pageNum == r.pageNum && slotNum == r.slotNum;
    }
    inline bool operator!=(const RID& r) const {
        return !(*this == r);
    }

    bool IsValid() const;  

private:

    PageNum pageNum;
    SlotNum slotNum;
    bool valid;             // true if set via parameterized constructor or copy
};

#endif  // RM_RID_H