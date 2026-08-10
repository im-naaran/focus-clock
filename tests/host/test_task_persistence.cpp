#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "persistence_codec.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    fprintf(stderr, "FAIL: %s\n", message);
  }
}

void testScheduledTaskRecordsRoundTrip() {
  ScheduledTaskRecords records;
  records.lastAttemptDateKeys[0] = 20260809;
  records.lastAttemptDateKeys[1] = 20240229;
  records.lastAttemptDateKeys[2] = 0;
  records.lastAttemptDateKeys[3] = 20991231;

  PersistedScheduledTaskRecordsV1 blob;
  memset(&blob, 0xFF, sizeof(blob));
  expect(persistenceEncodeScheduledTaskRecords(records, blob),
         "valid task records encode");
  expect(sizeof(blob) == 20, "task records V1 size is stable");
  expect(blob.version == SCHEDULED_TASK_RECORDS_BLOB_VERSION,
         "task records contain current version");
  expect(blob.slotCount == SCHEDULED_TASK_SLOT_COUNT,
         "task records contain fixed slot count");
  expect(blob.reserved[0] == 0 && blob.reserved[1] == 0,
         "task record reserved bytes are zero");

  ScheduledTaskRecords decoded;
  expect(persistenceDecodeScheduledTaskRecords(&blob, sizeof(blob), decoded) ==
             TaskPersistenceBlobError::None,
         "valid task records decode");
  expect(memcmp(records.lastAttemptDateKeys,
                decoded.lastAttemptDateKeys,
                sizeof(records.lastAttemptDateKeys)) == 0,
         "decoded task records match all four slots");
}

void testScheduledTaskRecordsErrors() {
  ScheduledTaskRecords records;
  PersistedScheduledTaskRecordsV1 blob;
  expect(persistenceEncodeScheduledTaskRecords(records, blob),
         "empty task records encode");

  ScheduledTaskRecords decoded;
  expect(persistenceDecodeScheduledTaskRecords(&blob, sizeof(blob) - 1,
                                                decoded) ==
             TaskPersistenceBlobError::InvalidSize,
         "truncated task records are rejected");

  PersistedScheduledTaskRecordsV1 invalid = blob;
  invalid.version += 1;
  expect(persistenceDecodeScheduledTaskRecords(&invalid, sizeof(invalid),
                                                decoded) ==
             TaskPersistenceBlobError::InvalidVersion,
         "unknown task record version is rejected");

  invalid = blob;
  invalid.slotCount = SCHEDULED_TASK_SLOT_COUNT - 1;
  expect(persistenceDecodeScheduledTaskRecords(&invalid, sizeof(invalid),
                                                decoded) ==
             TaskPersistenceBlobError::InvalidSlotCount,
         "changed task slot count is rejected");

  invalid = blob;
  invalid.lastAttemptDateKeys[3] = 20260229;
  expect(persistenceDecodeScheduledTaskRecords(&invalid, sizeof(invalid),
                                                decoded) ==
             TaskPersistenceBlobError::InvalidDateKey,
         "invalid persisted date key is rejected");

  records.lastAttemptDateKeys[0] = 20260431;
  expect(!persistenceEncodeScheduledTaskRecords(records, blob),
         "invalid runtime date key cannot encode");
}

void testTimeSyncResultRoundTrip() {
  PersistedTimeSyncResultV1 blob;
  memset(&blob, 0xFF, sizeof(blob));
  expect(persistenceEncodeTimeSyncResult(1786233605UL, blob),
         "valid time sync epoch encodes");
  expect(sizeof(blob) == 8, "time sync result V1 size is stable");
  expect(blob.version == TIME_SYNC_RESULT_BLOB_VERSION,
         "time sync result contains current version");
  expect(blob.reserved[0] == 0 && blob.reserved[1] == 0 &&
             blob.reserved[2] == 0,
         "time sync reserved bytes are zero");

  uint32_t decoded = 0;
  expect(persistenceDecodeTimeSyncResult(&blob, sizeof(blob), decoded) ==
             TaskPersistenceBlobError::None &&
             decoded == 1786233605UL,
         "valid time sync epoch round-trips");

  expect(persistenceEncodeTimeSyncResult(0, blob),
         "zero time sync epoch represents no result");
  decoded = 1;
  expect(persistenceDecodeTimeSyncResult(&blob, sizeof(blob), decoded) ==
             TaskPersistenceBlobError::None &&
             decoded == 0,
         "empty time sync result round-trips");
}

void testTimeSyncResultBounds() {
  PersistedTimeSyncResultV1 blob;
  expect(persistenceEncodeTimeSyncResult(946656000UL, blob),
         "first UTC epoch mapping to local year 2000 is accepted");
  expect(persistenceEncodeTimeSyncResult(4102415999UL, blob),
         "last UTC epoch mapping to local year 2099 is accepted");
  expect(!persistenceEncodeTimeSyncResult(946655999UL, blob),
         "epoch mapping before local year 2000 is rejected");
  expect(!persistenceEncodeTimeSyncResult(4102416000UL, blob),
         "epoch mapping to local year 2100 is rejected");

  expect(persistenceEncodeTimeSyncResult(1786233605UL, blob),
         "test epoch encodes before corruption");
  uint32_t decoded = 0;
  expect(persistenceDecodeTimeSyncResult(&blob, sizeof(blob) - 1, decoded) ==
             TaskPersistenceBlobError::InvalidSize,
         "truncated time sync result is rejected");

  PersistedTimeSyncResultV1 invalid = blob;
  invalid.version += 1;
  expect(persistenceDecodeTimeSyncResult(&invalid, sizeof(invalid), decoded) ==
             TaskPersistenceBlobError::InvalidVersion,
         "unknown time sync version is rejected");

  invalid = blob;
  invalid.lastSuccessEpoch = 4102416000UL;
  expect(persistenceDecodeTimeSyncResult(&invalid, sizeof(invalid), decoded) ==
             TaskPersistenceBlobError::InvalidEpoch,
         "persisted out-of-range epoch is rejected");
}

}  // namespace

int main() {
  testScheduledTaskRecordsRoundTrip();
  testScheduledTaskRecordsErrors();
  testTimeSyncResultRoundTrip();
  testTimeSyncResultBounds();

  if (failures != 0) {
    fprintf(stderr, "%d task persistence test(s) failed\n", failures);
    return 1;
  }
  printf("task persistence tests passed\n");
  return 0;
}
