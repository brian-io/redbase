```cpp
//
// File:        rm_test.cc
// Description: Main integration test for the Record Manager (RM)
//
// The test exercises the public RM interface:
//
//   CreateFile
//   OpenFile
//   InsertRec
//   GetRec
//   UpdateRec
//   DeleteRec
//   RM_FileScan
//   CloseFile
//   Reopen / persistence
//   DestroyFile
//
// The test deliberately uses enough records to force the RM layer to
// allocate multiple data pages.
//
//

#include <cstdio>
#include <cstring>
#include <iostream>

#include "rm.h"

using namespace std;

#define TEST_FILE "rm_test_file"


//
// Simple fixed-size record used by the test.
//
// Record layout:
//
//   offset 0  : int id
//   offset 4  : int value
//   offset 8  : char name[16]
//
// Total size = 24 bytes.
//

struct TestRecord {
    int id;
    int value;
    char name[16];
};

static const int RECORD_COUNT = 100;


//
// Check an RC and stop the test on failure.
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


//
// Build a deterministic test record.
//
static TestRecord MakeRecord(int id)
{
    TestRecord record;

    record.id = id;
    record.value = id * 10;

    snprintf(record.name,
             sizeof(record.name),
             "record_%03d",
             id);

    return record;
}


//
// Verify a record against the expected values.
//
static bool VerifyRecord(const TestRecord &actual, int id)
{
    TestRecord expected = MakeRecord(id);

    return actual.id == expected.id &&
           actual.value == expected.value &&
           memcmp(actual.name,
                  expected.name,
                  sizeof(expected.name)) == 0;
}


//
// Test insertion and retrieval.
//
static RC TestInsertAndGet(RM_FileHandle &fileHandle,
                           RID rids[])
{
    cout << "Testing InsertRec + GetRec: ";

    for (int i = 0; i < RECORD_COUNT; ++i) {
        TestRecord record = MakeRecord(i);
        RID rid;

        RC rc = fileHandle.InsertRec(
            reinterpret_cast<const char *>(&record),
            rid
        );

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "InsertRec failed for record "
                 << i << "\n";
            RM_PrintError(rc);
            return rc;
        }

        rids[i] = rid;

        RM_Record retrieved;

        rc = fileHandle.GetRec(rid, retrieved);

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "GetRec failed for record "
                 << i << "\n";
            RM_PrintError(rc);
            return rc;
        }

        char *data = nullptr;

        rc = retrieved.GetData(data);

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "RM_Record::GetData failed\n";
            RM_PrintError(rc);
            return rc;
        }

        if (data == nullptr) {
            cout << "FAILED\n";
            cout << "GetData returned nullptr\n";
            return 1;
        }

        TestRecord actual;

        memcpy(&actual, data, sizeof(TestRecord));

        if (!VerifyRecord(actual, i)) {
            cout << "FAILED\n";
            cout << "Retrieved record does not match "
                 << "inserted record " << i << "\n";
            return 1;
        }
    }

    cout << "Pass\n";
    return 0;
}


//
// Test updating an existing record.
//
static RC TestUpdate(RM_FileHandle &fileHandle,
                      const RID &rid)
{
    cout << "Testing UpdateRec: ";

    RM_Record record;

    RC rc = fileHandle.GetRec(rid, record);

    if (rc != 0) {
        cout << "FAILED\n";
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

    TestRecord updated;

    memcpy(&updated, data, sizeof(TestRecord));

    //
    // Change only the value.
    //
    updated.value = 999999;

    //
    // RM_Record owns its internal copy, so we need a record containing
    // the modified data. Since RM_Record cannot be directly constructed
    // from arbitrary data through the public interface, retrieve the
    // record and use the existing record's internal representation.
    //
    // The public RM API exposes UpdateRec(const RM_Record&), so this
    // integration test relies on GetRec returning a record whose data
    // can be modified through GetData().
    //
    memcpy(data, &updated, sizeof(TestRecord));

    rc = fileHandle.UpdateRec(record);

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    //
    // Read it again and verify the modification persisted.
    //
    RM_Record verifyRecord;

    rc = fileHandle.GetRec(rid, verifyRecord);

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    char *verifyData = nullptr;

    rc = verifyRecord.GetData(verifyData);

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    TestRecord actual;

    memcpy(&actual, verifyData, sizeof(TestRecord));

    if (actual.value != 999999) {
        cout << "FAILED\n";
        cout << "Updated value was not persisted\n";
        return 1;
    }

    cout << "Pass\n";
    return 0;
}


//
// Test deletion and invalidation of deleted records.
//
static RC TestDelete(RM_FileHandle &fileHandle,
                      const RID rids[])
{
    cout << "Testing DeleteRec: ";

    //
    // Delete every second record.
    //
    for (int i = 0; i < RECORD_COUNT; i += 2) {
        RC rc = fileHandle.DeleteRec(rids[i]);

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "DeleteRec failed for record "
                 << i << "\n";
            RM_PrintError(rc);
            return rc;
        }
    }

    //
    // Deleted records must no longer be retrievable.
    //
    for (int i = 0; i < RECORD_COUNT; i += 2) {
        RM_Record record;

        RC rc = fileHandle.GetRec(rids[i], record);

        if (rc != RM_RECORDNOTFOUND) {
            cout << "FAILED\n";
            cout << "Deleted record " << i
                 << " was still retrievable\n";

            if (rc != 0)
                RM_PrintError(rc);

            return 1;
        }
    }

    //
    // Records that were not deleted must still exist.
    //
    for (int i = 1; i < RECORD_COUNT; i += 2) {
        RM_Record record;

        RC rc = fileHandle.GetRec(rids[i], record);

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "Existing record " << i
                 << " disappeared after deletion\n";
            RM_PrintError(rc);
            return rc;
        }
    }

    cout << "Pass\n";
    return 0;
}


//
// Test conditional scanning.
//
// We scan the first integer field:
//
//     id >= 50
//
// Since all even records were deleted, the expected matching IDs are:
//
//     51, 53, 55, ..., 99
//
// There are 25 of them.
//
static RC TestScan(RM_FileHandle &fileHandle)
{
    cout << "Testing RM_FileScan: ";

    int minimumID = 50;

    RM_FileScan scan;

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

    int expectedCount = 0;

    while (true) {
        RM_Record record;

        rc = scan.GetNextRec(record);

        if (rc == RM_EOF)
            break;

        if (rc != 0) {
            cout << "FAILED\n";
            cout << "GetNextRec failed\n";
            RM_PrintError(rc);

            scan.CloseScan();

            return rc;
        }

        char *data = nullptr;

        rc = record.GetData(data);

        if (rc != 0) {
            cout << "FAILED\n";
            RM_PrintError(rc);

            scan.CloseScan();

            return rc;
        }

        TestRecord testRecord;

        memcpy(&testRecord, data, sizeof(TestRecord));

        //
        // Because even IDs were deleted, every returned ID should
        // be odd and >= 51.
        //
        if (testRecord.id < 50 ||
            testRecord.id % 2 == 0) {

            cout << "FAILED\n";
            cout << "Scan returned unexpected record ID "
                 << testRecord.id << "\n";

            scan.CloseScan();

            return 1;
        }

        ++expectedCount;
    }

    rc = scan.CloseScan();

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "CloseScan failed\n";
        RM_PrintError(rc);
        return rc;
    }

    //
    // IDs 51 through 99, odd only:
    //
    // 51, 53, ..., 99 = 25 records.
    //
    if (expectedCount != 25) {
        cout << "FAILED\n";
        cout << "Expected 25 matching records, got "
             << expectedCount << "\n";
        return 1;
    }

    cout << "Pass\n";
    return 0;
}


//
// Verify that data survives closing and reopening the RM file.
//
static RC TestPersistence(RM_Manager &rmm,
                          const RID rids[])
{
    cout << "Testing CloseFile + OpenFile persistence: ";

    RM_FileHandle fileHandle;

    RC rc = rmm.OpenFile(TEST_FILE, fileHandle);

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "OpenFile failed\n";
        RM_PrintError(rc);
        return rc;
    }

    //
    // Record 1 was not deleted.
    //
    RM_Record record;

    rc = fileHandle.GetRec(rids[1], record);

    if (rc != 0) {
        cout << "FAILED\n";
        cout << "Record 1 could not be retrieved after reopen\n";
        RM_PrintError(rc);

        rmm.CloseFile(fileHandle);

        return rc;
    }

    char *data = nullptr;

    rc = record.GetData(data);

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);

        rmm.CloseFile(fileHandle);

        return rc;
    }

    TestRecord actual;

    memcpy(&actual, data, sizeof(TestRecord));

    if (!VerifyRecord(actual, 1)) {
        cout << "FAILED\n";
        cout << "Persisted record does not match expected data\n";

        rmm.CloseFile(fileHandle);

        return 1;
    }

    //
    // Record 0 was deleted and must remain deleted after reopen.
    //
    RM_Record deletedRecord;

    rc = fileHandle.GetRec(rids[0], deletedRecord);

    if (rc != RM_RECORDNOTFOUND) {
        cout << "FAILED\n";
        cout << "Deleted record became visible after reopen\n";

        rmm.CloseFile(fileHandle);

        return 1;
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
// Main integration test.
//
static RC TestRM()
{
    PF_Manager pfm;
    RM_Manager rmm(pfm);

    RM_FileHandle fileHandle;

    RID rids[RECORD_COUNT];

    RC rc;

    //
    // Make the test repeatable.
    //
    rmm.DestroyFile(TEST_FILE);

    //
    // 1. Create the RM file.
    //
    cout << "Creating RM file: ";

    rc = rmm.CreateFile(
        TEST_FILE,
        sizeof(TestRecord)
    );

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    cout << "Pass\n";


    //
    // 2. Open the RM file.
    //
    cout << "Opening RM file: ";

    rc = rmm.OpenFile(
        TEST_FILE,
        fileHandle
    );

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    cout << "Pass\n";


    //
    // 3. Insert records and immediately retrieve them.
    //
    rc = TestInsertAndGet(
        fileHandle,
        rids
    );

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }


    //
    // 4. Update one existing record.
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
    // 5. Delete half the records.
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
    // 6. Scan remaining records using a condition.
    //
    rc = TestScan(fileHandle);

    if (rc != 0) {
        rmm.CloseFile(fileHandle);
        return rc;
    }


    //
    // 7. Close the file.
    //
    cout << "Closing RM file: ";

    rc = rmm.CloseFile(fileHandle);

    if (rc != 0) {
        cout << "FAILED\n";
        RM_PrintError(rc);
        return rc;
    }

    cout << "Pass\n";


    //
    // 8. Reopen and verify persistence.
    //
    rc = TestPersistence(
        rmm,
        rids
    );

    if (rc != 0)
        return rc;


    //
    // 9. Destroy the file.
    //
    cout << "Destroying RM file: ";

    rc = rmm.DestroyFile(TEST_FILE);

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
    cout << "-----------------------------------\n";

    RC rc = TestRM();

    if (rc != 0) {
        cout << "\nRM module test FAILED.\n";
        return 1;
    }

    cout << "-----------------------------------\n";
    cout << "RM module test PASSED.\n";

    return 0;
}
