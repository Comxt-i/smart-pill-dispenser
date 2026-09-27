#pragma once
#include <Arduino.h>
void commandJournalBegin();
void commandJournalDoseKey(char *out, size_t size, const char *scheduleId, uint32_t day, bool dryRun);
bool commandJournalContains(const char *id);
// Persists before motion; false means do NOT execute (storage full/unavailable).
bool commandJournalReserve(const char *id, uint32_t epoch);
