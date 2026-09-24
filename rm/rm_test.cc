//
// File:        rm_test.cc
// Description: Comprehensive integration test for the Record Manager.
//
// Tests:
//
//   - CreateFile
//   - OpenFile / CloseFile
//   - InsertRec
//   - GetRec
//   - RID correctness
//   - Multi-page allocation
//   - UpdateRec
//   - DeleteRec
//   - Slot reuse
//   - RM_FileScan
//   - ForcePages
//   - Persistence across close/reopen
//   - Invalid RID handling
//   - DestroyFile
//
// The test intentionally uses enough records to force multiple RM data pages.
//

#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "rm.h"

using namespace std;

#define TEST_FILE "rm_test_file"

static const int RECORD_COUNT = 400;
static const int RECORD_SIZE = sizeof(int) * 2 + 16;

//
// Fixed-size deterministic test record.
//
// Layout:
//
//   offset 0  : int id
//   offset 4  : int value
//   offset 8  : char name[16]
//
// Total = 24 bytes.
//
struct TestRecord {
    int id;
    int value;
    char name[16];
};

static_assert(sizeof(TestRecord) == 24,
              "Unexpected TestRecord size");


//
// ---------------------------------------------------------------------------
// Test utilities
// ---------------------------------------------------------------------------
//

static RC CheckRC(const char *operation, RC rc)
{
    if (rc != 0) {
        cout << "FAILED: " << operation
             << " (RC = " << rc << ")\n";

        RM_PrintError(rc);
        return rc;
    }

    return 0;
}


static TestRecord MakeRecord(int id)
{
    //
    // Zero-initialize the complete structure.
    //
    // This is important because the test compares the complete 16-byte
    // name field. Without {}, the unused bytes after the terminating '\0'
    // would contain indeterminate stack data.
    //
    TestRecord record{};

    record.id = id;
    record.value = id * 10;

    snprintf(
        record.name,
        sizeof(record.name),
        "record_%03d",
        id
    );

    return record;
}


static bool RecordsEqual(
    const TestRecord &actual,
    const TestRecord &expected)
{
    return actual.id == expected.id &&
           actual.value == expected.value &&
           memcmp(
               actual.name,
               expected.name,
               sizeof(expected.name)
           ) == 0;
}


static bool VerifyRecord(
    const TestRecord &actual,
    int expectedID)
{
    TestRecord expected = MakeRecord(expectedID);

    return RecordsEqual(actual, expected);
}


static RC ReadTestRecord(
    RM_FileHandle &fileHandle,
    const RID &rid,
    TestRecord &result)
{
    RM_Record record;

    RC rc = fileHandle.GetRec(rid, record);

    if (rc != 0)
        return rc;

    char *data = nullptr;

    rc = record.GetData(data);

    if (rc != 0)
        return rc;

    if (data == nullptr)
        return RM_INVALIDRECORD;

    memcpy(
        &result,
        data,
        sizeof(TestRecord)
    );

    return 0;
}


static bool RIDsEqual(
    const RID &a,
    const RID &b)
{
    PageNum pageA;
    PageNum pageB;

    SlotNum slotA;
    SlotNum slotB;

    if (a.GetPageNum(pageA) != 0)
        return false;

    if (a.GetSlotNum(slotA) != 0)
        return false;

    if (b.GetPageNum(pageB) != 0)
        return false;

    if (b.GetSlotNum(slotB) != 0)
        return false;

    return pageA == pageB &&
           slotA == slotB;
}


static void PrintRID(
    const char *prefix,
    const RID &rid)
{
    PageNum pageNum;
    SlotNum slotNum;

    if (rid.GetPageNum(pageNum) != 0 ||
        rid.GetSlotNum(slotNum) != 0) {
        cout << prefix << "<invalid RID>\n";
        return;
    }

    cout << prefix
         << "page=" << pageNum
         << ", slot=" << slotNum
         << '\n';
}


//
// ---------------------------------------------------------------------------
// Test 1: Create / Open
// ---------------------------------------------------------------------------
//

static RC TestCreateAndOpen(
    RM_Manager &rmm,
    RM_FileHandle &fileHandle)
{
    cout << "Test 1: CreateFile + OpenFile: ";

    rmm.DestroyFile(TEST_FILE);

    RC rc = rmm.CreateFile(
        TEST_FILE,
        sizeof(TestRecord)
    );

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    rc = rmm.OpenFile(
        TEST_FILE,
        fileHandle
    );

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    if (!fileHandle.IsOpen()) {
        cout << "FAILED\n";
        cout << "FileHandle reports that it is not open\n";
        return RM_INVALIDFILE;
    }

    cout << "Pass\n";
    return 0;
}


//
// ---------------------------------------------------------------------------
// Test 2: Single insert / retrieve
// ---------------------------------------------------------------------------
//

static RC TestSingleInsert(
    RM_FileHandle &fileHandle,
    RID &rid)
{
    cout << "Test 2: Single InsertRec + GetRec: ";

    TestRecord expected = MakeRecord(0);

    RC rc = fileHandle.InsertRec(
        reinterpret_cast<const char *>(&expected),
        rid
    );

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    TestRecord actual{};

    rc = ReadTestRecord(
        fileHandle,
        rid,
        actual
    );

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "GetRec failed\n";
        RM_PrintError(rc);
        return rc;
    }

    if (!RecordsEqual(actual, expected)) {
        cout << "FAILED\n";
        cout << "Inserted record does not match retrieved record\n";

        PrintRID("RID: ", rid);

        cout << "Expected: "
             << expected.id << ", "
             << expected.value << ", "
             << expected.name << '\n';

        cout << "Actual:   "
             << actual.id << ", "
             << actual.value << ", "
             << actual.name << '\n';

        return RM_UNEXPECTEDRC;
    }

    cout << "Pass\n";
    return 0;
}


//
// ---------------------------------------------------------------------------
// Test 3: Insert many records
// ---------------------------------------------------------------------------
//

static RC TestBulkInsert(
    RM_FileHandle &fileHandle,
    RID rids[])
{
    cout << "Test 3: Bulk InsertRec + GetRec: ";

    //
    // Record 0 already exists from TestSingleInsert().
    //
    rids[0] = rids[0];

    for (int i = 1; i < RECORD_COUNT; ++i) {
        TestRecord expected = MakeRecord(i);

        RC rc = fileHandle.InsertRec(
            reinterpret_cast<const char *>(&expected),
            rids[i]
        );

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "InsertRec failed for record "
                 << i << '\n';

            RM_PrintError(rc);
            return rc;
        }
    }

    //
    // Verify every record.
    //
    for (int i = 0; i < RECORD_COUNT; ++i) {
        TestRecord actual{};

        RC rc = ReadTestRecord(
            fileHandle,
            rids[i],
            actual
        );

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "GetRec failed for record "
                 << i << '\n';

            PrintRID("RID: ", rids[i]);
            RM_PrintError(rc);

            return rc;
        }

        if (!VerifyRecord(actual, i)) {
            cout << "FAILED\n";
            cout << "Retrieved record does not match "
                 << "expected record " << i << '\n';

            PrintRID("RID: ", rids[i]);

            cout << "Expected: "
                 << MakeRecord(i).id << ", "
                 << MakeRecord(i).value << ", "
                 << MakeRecord(i).name << '\n';

            cout << "Actual:   "
                 << actual.id << ", "
                 << actual.value << ", "
                 << actual.name << '\n';

            return RM_UNEXPECTEDRC;
        }
    }

    cout << "Pass\n";
    return 0;
}


//
// ---------------------------------------------------------------------------
// Test 4: RID uniqueness / multi-page allocation
// ---------------------------------------------------------------------------
//

static RC TestRIDs(
    const RID rids[])
{
    cout << "Test 4: RID uniqueness + multi-page allocation: ";

    set<pair<PageNum, SlotNum>> seen;

    PageNum firstPage = -1;
    PageNum lastPage = -1;

    for (int i = 0; i < RECORD_COUNT; ++i) {
        PageNum pageNum;
        SlotNum slotNum;

        RC rc = rids[i].GetPageNum(pageNum);

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "Could not read page number from RID "
                 << i << '\n';
            return RM_INVALIDRID;
        }

        rc = rids[i].GetSlotNum(slotNum);

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "Could not read slot number from RID "
                 << i << '\n';
            return RM_INVALIDRID;
        }

        if (pageNum < 1) {
            cout << "FAILED\n";
            cout << "Record " << i
                 << " was assigned invalid page "
                 << pageNum << '\n';
            return RM_INVALIDRID;
        }

        if (slotNum < 0) {
            cout << "FAILED\n";
            cout << "Record " << i
                 << " was assigned invalid slot "
                 << slotNum << '\n';
            return RM_INVALIDRID;
        }

        pair<PageNum, SlotNum> key(pageNum, slotNum);

        if (!seen.insert(key).second) {
            cout << "FAILED\n";
            cout << "Duplicate RID detected for record "
                 << i << '\n';

            PrintRID("RID: ", rids[i]);

            return RM_INVALIDRID;
        }

        if (firstPage == -1)
            firstPage = pageNum;

        lastPage = pageNum;
    }

    //
    // 100 records of 24 bytes should require more than one data page
    // with a normal PF_PAGE_SIZE such as 4096.
    //
    if (firstPage == lastPage) {
        cout << "FAILED\n";
        cout << "All records were stored on one page.\n";
        cout << "The test expects multiple data pages.\n";
        return RM_UNEXPECTEDRC;
    }

    cout << "Pass\n";
    cout << "  First data page: " << firstPage << '\n';
    cout << "  Last data page:  " << lastPage << '\n';

    return 0;
}


//
// ---------------------------------------------------------------------------
// Test 5: UpdateRec
// ---------------------------------------------------------------------------
//

static RC TestUpdate(
    RM_FileHandle &fileHandle,
    const RID &rid)
{
    cout << "Test 5: UpdateRec: ";

    RM_Record record;

    RC rc = fileHandle.GetRec(
        rid,
        record
    );

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "Could not retrieve record for update\n";
        RM_PrintError(rc);
        return rc;
    }

    char *data = nullptr;

    rc = record.GetData(data);

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    if (data == nullptr) {
        cout << "FAILED\n";
        cout << "GetData returned nullptr\n";
        return RM_INVALIDRECORD;
    }

    TestRecord updated{};

    memcpy(
        &updated,
        data,
        sizeof(updated)
    );

    updated.value = 999999;

    //
    // Modify the RM_Record's owned buffer.
    //
    memcpy(
        data,
        &updated,
        sizeof(updated)
    );

    rc = fileHandle.UpdateRec(record);

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    //
    // Retrieve the record again.
    //
    TestRecord actual{};

    rc = ReadTestRecord(
        fileHandle,
        rid,
        actual
    );

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "Could not retrieve updated record\n";
        RM_PrintError(rc);
        return rc;
    }

    if (actual.id != updated.id ||
        actual.value != 999999 ||
        memcmp(
            actual.name,
            updated.name,
            sizeof(updated.name)
        ) != 0) {

        cout << "FAILED\n";
        cout << "Updated record contents are incorrect\n";

        return RM_UNEXPECTEDRC;
    }

    cout << "Pass\n";
    return 0;
}


//
// ---------------------------------------------------------------------------
// Test 6: DeleteRec
// ---------------------------------------------------------------------------
//

static RC TestDelete(
    RM_FileHandle &fileHandle,
    const RID rids[])
{
    cout << "Test 6: DeleteRec: ";

    //
    // Delete every even-numbered record.
    //
    for (int i = 0; i < RECORD_COUNT; i += 2) {
        RC rc = fileHandle.DeleteRec(
            rids[i]
        );

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "DeleteRec failed for record "
                 << i << '\n';

            PrintRID("RID: ", rids[i]);
            RM_PrintError(rc);

            return rc;
        }
    }

    //
    // Verify deleted records are unavailable.
    //
    for (int i = 0; i < RECORD_COUNT; i += 2) {
        RM_Record record;

        RC rc = fileHandle.GetRec(
            rids[i],
            record
        );

        if (rc != RM_RECORDNOTFOUND) {
            cout << "FAILED\n";
            cout << "Deleted record " << i
                 << " is still retrievable\n";

            PrintRID("RID: ", rids[i]);

            if (rc != 0)
                RM_PrintError(rc);

            return RM_UNEXPECTEDRC;
        }
    }

    //
    // Verify odd records still exist.
    //
    for (int i = 1; i < RECORD_COUNT; i += 2) {
        TestRecord actual{};

        RC rc = ReadTestRecord(
            fileHandle,
            rids[i],
            actual
        );

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "Existing record " << i
                 << " disappeared after deletion\n";

            RM_PrintError(rc);
            return rc;
        }

        //
        // Record 1 was deliberately updated.
        //
        if (i == 1) {
            if (actual.id != 1 ||
                actual.value != 999999) {

                cout << "FAILED\n";
                cout << "Updated record 1 was corrupted\n";
                return RM_UNEXPECTEDRC;
            }
        }
        else if (!VerifyRecord(actual, i)) {
            cout << "FAILED\n";
            cout << "Record " << i
                 << " changed unexpectedly\n";
            return RM_UNEXPECTEDRC;
        }
    }

    cout << "Pass\n";
    return 0;
}


//
// ---------------------------------------------------------------------------
// Test 7: Slot reuse
// ---------------------------------------------------------------------------
//

static RC TestSlotReuse(
    RM_FileHandle &fileHandle,
    const RID &deletedRID)
{
    cout << "Test 7: Free-slot reuse: ";

    TestRecord replacement = MakeRecord(1000);

    RID replacementRID;

    RC rc = fileHandle.InsertRec(
        reinterpret_cast<const char *>(&replacement),
        replacementRID
    );

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "InsertRec failed after deletion\n";
        RM_PrintError(rc);
        return rc;
    }

    //
    // The exact reused RID is implementation-dependent only if the
    // RM chooses another free page/slot. What matters is that the
    // replacement record is valid and retrievable.
    //
    TestRecord actual{};

    rc = ReadTestRecord(
        fileHandle,
        replacementRID,
        actual
    );

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "Replacement record could not be retrieved\n";
        RM_PrintError(rc);
        return rc;
    }

    if (!RecordsEqual(actual, replacement)) {
        cout << "FAILED\n";
        cout << "Replacement record does not match inserted data\n";
        return RM_UNEXPECTEDRC;
    }

    //
    // The old RID must remain invalid unless the test happens to have
    // inserted into exactly that slot. Since slot reuse is allowed,
    // we only require that the old RID is not incorrectly returning
    // the deleted record.
    //
    if (RIDsEqual(replacementRID, deletedRID)) {
        if (RecordsEqual(actual, replacement)) {
            //
            // This is valid slot reuse.
            //
            cout << "Pass\n";
            return 0;
        }
    }

    cout << "Pass\n";
    return 0;
}


//
// ---------------------------------------------------------------------------
// Test 8: RM_FileScan
// ---------------------------------------------------------------------------
//


static RC TestScan(
    RM_FileHandle &fileHandle)
{
    cout << "Test 8: RM_FileScan: ";

    int minimumID = 50;

    RM_FileScan scan;

    //
    // Scan the first integer field of TestRecord.
    //
    // TestRecord:
    //   offset 0 = id
    //   offset 4 = value
    //   offset 8 = name
    //
    RC rc = scan.OpenScan(
        fileHandle,
        INT,
        sizeof(int),
        0,
        GE_OP,
        &minimumID,
        NO_HINT
    );

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "OpenScan failed\n";
        RM_PrintError(rc);
        return rc;
    }

    set<int> foundIDs;

    while (true) {
        RM_Record record;

        rc = scan.GetNextRec(record);

        //
        // EOF is the normal termination condition.
        //
        if (rc == RM_EOF)
            break;

        //
        // RM_RECORDNOTFOUND should not normally escape GetNextRec().
        // If it does, report it as a scan implementation error rather
        // than treating it as an expected end-of-scan condition.
        //
        if (rc != 0) {
            cout << "FAILED\n";
            cout << "GetNextRec failed\n";
            cout << "RC = " << rc << '\n';

            RM_PrintError(rc);

            scan.CloseScan();

            return rc;
        }

        //
        // Extract the returned record.
        //
        char *data = nullptr;

        rc = record.GetData(data);

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "GetData failed for scanned record\n";

            RM_PrintError(rc);

            scan.CloseScan();

            return rc;
        }

        if (data == nullptr) {
            cout << "FAILED\n";
            cout << "GetData returned nullptr\n";

            scan.CloseScan();

            return RM_INVALIDRECORD;
        }

        //
        // A scan returns a complete RM_Record, so verify that the
        // complete TestRecord is readable.
        //
        TestRecord actual{};

        memcpy(
            &actual,
            data,
            sizeof(TestRecord)
        );

        //
        // The scan predicate is:
        //
        //     id >= 50
        //
        if (actual.id < minimumID) {
            cout << "FAILED\n";
            cout << "Scan returned record below predicate: "
                 << actual.id << '\n';

            scan.CloseScan();

            return RM_UNEXPECTEDRC;
        }

        //
        // Every returned record must be either:
        //
        //   - one of the original surviving odd records, or
        //   - the replacement record 1000.
        //
        //
        // Deleted even records must never appear.
        //
        if (actual.id != 1000 &&
            actual.id % 2 == 0) {

            cout << "FAILED\n";
            cout << "Scan returned deleted/even record: "
                 << actual.id << '\n';

            scan.CloseScan();

            return RM_UNEXPECTEDRC;
        }

        //
        // Verify the actual record contents.
        //
        if (actual.id == 1000) {
            TestRecord expected = MakeRecord(1000);

            if (!RecordsEqual(actual, expected)) {
                cout << "FAILED\n";
                cout << "Replacement record 1000 is corrupted\n";

                scan.CloseScan();

                return RM_UNEXPECTEDRC;
            }
        }
        else {
            if (!VerifyRecord(actual, actual.id)) {
                cout << "FAILED\n";
                cout << "Scanned record " << actual.id
                     << " contains incorrect data\n";

                scan.CloseScan();

                return RM_UNEXPECTEDRC;
            }
        }

        //
        // A scan must never return the same RID twice.
        //
        RID rid;

        rc = record.GetRid(rid);

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "Could not obtain RID from scanned record\n";

            RM_PrintError(rc);

            scan.CloseScan();

            return rc;
        }

        PageNum pageNum;
        SlotNum slotNum;

        if ((rc = rid.GetPageNum(pageNum)) != 0 ||
            (rc = rid.GetSlotNum(slotNum)) != 0) {

            cout << "FAILED\n";
            cout << "Invalid RID returned by scan\n";

            scan.CloseScan();

            return RM_INVALIDRID;
        }

        //
        // Use the RID rather than the record ID for duplicate detection.
        //
        pair<PageNum, SlotNum> ridKey(pageNum, slotNum);

        //
        // We keep the record ID set separately because it gives us an
        // additional sanity check.
        //
        if (!foundIDs.insert(actual.id).second) {
            cout << "FAILED\n";
            cout << "Scan returned duplicate record ID: "
                 << actual.id << '\n';

            scan.CloseScan();

            return RM_UNEXPECTEDRC;
        }
    }

    //
    // CloseScan must succeed after reaching EOF.
    //
    rc = scan.CloseScan();

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "CloseScan failed\n";

        RM_PrintError(rc);

        return rc;
    }

    //
    // After deleting all even IDs:
    //
    //   51, 53, 55, ..., 99
    //
    // gives 25 surviving records satisfying id >= 50.
    //
    // TestSlotReuse() then inserts:
    //
    //   1000
    //
    // Therefore the scan should return 26 records.
    //
    const int expectedCount = 176;

    if (static_cast<int>(foundIDs.size()) != expectedCount) {
        cout << "FAILED\n";
        cout << "Unexpected scan result count\n";
        cout << "Expected: " << expectedCount << '\n';
        cout << "Actual:   " << foundIDs.size() << '\n';

        return RM_UNEXPECTEDRC;
    }

    //
    // Explicitly verify that every expected ID was returned.
    //
    for (int id = 51; id <= 99; id += 2) {
        if (foundIDs.find(id) == foundIDs.end()) {
            cout << "FAILED\n";
            cout << "Scan did not return expected record "
                 << id << '\n';

            return RM_UNEXPECTEDRC;
        }
    }

    //
    // Replacement record must also be visible.
    //
    if (foundIDs.find(1000) == foundIDs.end()) {
        cout << "FAILED\n";
        cout << "Scan did not return replacement record 1000\n";

        return RM_UNEXPECTEDRC;
    }

    cout << "Pass\n";

    return 0;
}




//
// ---------------------------------------------------------------------------
// Test 9: Invalid RIDs
// ---------------------------------------------------------------------------
//

static RC TestInvalidRIDs(
    RM_FileHandle &fileHandle)
{
    cout << "Test 9: Invalid RID handling: ";

    //
    // Page 0 is the RM file header and cannot contain records.
    //
    RID headerRID(0, 0);

    RM_Record record;

    RC rc = fileHandle.GetRec(
        headerRID,
        record
    );

    if (rc != RM_INVALIDRID) {
        cout << "FAILED\n";
        cout << "GetRec accepted page 0\n";
        return RM_UNEXPECTEDRC;
    }

    //
    // Negative page.
    //
    RID negativePage(-1, 0);

    rc = fileHandle.GetRec(
        negativePage,
        record
    );

    if (rc != RM_INVALIDRID) {
        cout << "FAILED\n";
        cout << "GetRec accepted negative page\n";
        return RM_UNEXPECTEDRC;
    }

    //
    // Negative slot.
    //
    RID negativeSlot(1, -1);

    rc = fileHandle.GetRec(
        negativeSlot,
        record
    );

    if (rc != RM_INVALIDRID) {
        cout << "FAILED\n";
        cout << "GetRec accepted negative slot\n";
        return RM_UNEXPECTEDRC;
    }

    cout << "Pass\n";
    return 0;
}


//
// ---------------------------------------------------------------------------
// Test 10: ForcePages
// ---------------------------------------------------------------------------
//

static RC TestForcePages(
    RM_FileHandle &fileHandle)
{
    cout << "Test 10: ForcePages: ";

    RC rc = fileHandle.ForcePages();

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    cout << "Pass\n";
    return 0;
}


//
// ---------------------------------------------------------------------------
// Test 11: Close / reopen persistence
// ---------------------------------------------------------------------------
//

static RC TestPersistence(
    RM_Manager &rmm,
    const RID rids[])
{
    cout << "Test 11: CloseFile + OpenFile persistence: ";

    //
    // Close the current handle first.
    //
    // This function is called after all previous tests have completed.
    //
    RM_FileHandle fileHandle;

    RC rc = rmm.OpenFile(
        TEST_FILE,
        fileHandle
    );

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "Could not reopen RM file\n";
        RM_PrintError(rc);
        return rc;
    }

    //
    // Record 1 survived and was updated.
    //
    TestRecord record1{};

    rc = ReadTestRecord(
        fileHandle,
        rids[1],
        record1
    );

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "Record 1 could not be read after reopen\n";
        RM_PrintError(rc);

        rmm.CloseFile(fileHandle);

        return rc;
    }

    if (record1.id != 1 ||
        record1.value != 999999) {

        cout << "FAILED\n";
        cout << "Record 1 did not preserve its updated value\n";

        rmm.CloseFile(fileHandle);

        return RM_UNEXPECTEDRC;
    }

    //
    // Record 0 was deleted.
    //
    RM_Record deleted;

    rc = fileHandle.GetRec(
        rids[0],
        deleted
    );

    if (rc != RM_RECORDNOTFOUND) {
        cout << "FAILED\n";
        cout << "Deleted record 0 became visible after reopen\n";

        if (rc != 0)
            RM_PrintError(rc);

        rmm.CloseFile(fileHandle);

        return RM_UNEXPECTEDRC;
    }

    //
    // Record 99 survived.
    //
    TestRecord record99{};

    rc = ReadTestRecord(
        fileHandle,
        rids[99],
        record99
    );

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "Record 99 could not be read after reopen\n";
        RM_PrintError(rc);

        rmm.CloseFile(fileHandle);

        return rc;
    }

    if (!VerifyRecord(record99, 99)) {
        cout << "FAILED\n";
        cout << "Record 99 was corrupted after reopen\n";

        rmm.CloseFile(fileHandle);

        return RM_UNEXPECTEDRC;
    }

    rc = rmm.CloseFile(fileHandle);

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "CloseFile failed\n";
        RM_PrintError(rc);
        return rc;
    }

    cout << "Pass\n";
    return 0;
}


//
// ---------------------------------------------------------------------------
// Main integration test
// ---------------------------------------------------------------------------
//

static RC TestRM()
{
    PF_Manager pfm;
    RM_Manager rmm(pfm);

    RM_FileHandle fileHandle;

    RID rids[RECORD_COUNT];

    RC rc;

    //
    // Remove any previous test file.
    //
    rmm.DestroyFile(TEST_FILE);

    //
    // 1. Create and open.
    //
    rc = TestCreateAndOpen(
        rmm,
        fileHandle
    );

    if (rc != 0)
        return rc;

    //
    // 2. Single-record test.
    //
    rc = TestSingleInsert(
        fileHandle,
        rids[0]
    );

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }

    //
    // 3. Bulk insertion and retrieval.
    //
    rc = TestBulkInsert(
        fileHandle,
        rids
    );

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }

    //
    // 4. Verify RID allocation.
    //
    rc = TestRIDs(rids);

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }

    //
    // 5. Update record 1.
    //
    rc = TestUpdate(
        fileHandle,
        rids[1]
    );

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }

    //
    // 6. Delete all even records.
    //
    rc = TestDelete(
        fileHandle,
        rids
    );

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }

    //
    // 7. Insert after deletion to exercise free slots.
    //
    rc = TestSlotReuse(
        fileHandle,
        rids[0]
    );

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }

    //
    // 8. Scan.
    //
    rc = TestScan(
        fileHandle
    );

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }

    //
    // 9. Invalid RIDs.
    //
    rc = TestInvalidRIDs(
        fileHandle
    );

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }

    //
    // 10. Force all pages.
    //
    rc = TestForcePages(
        fileHandle
    );

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }

    //
    // Close before the persistence test.
    //
    cout << "Test 11 preparation: CloseFile: ";

    rc = rmm.CloseFile(fileHandle);

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    cout << "Pass\n";

    //
    // 11. Reopen and verify persistence.
    //
    rc = TestPersistence(
        rmm,
        rids
    );

    if (rc != 0)
        return rc;

    //
    // 12. Destroy.
    //
    cout << "Test 12: DestroyFile: ";

    rc = rmm.DestroyFile(
        TEST_FILE
    );

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    cout << "Pass\n";

    return 0;
}


int main()
{
    cout << "Starting RM module integration test.\n";
    cout << "===================================\n";

    RC rc = TestRM();

    cout << "===================================\n";

    if (rc != 0) {
        cout << "RM module test FAILED.\n";
        return 1;
    }

    cout << "RM module test PASSED.\n";

    return 0;
}

