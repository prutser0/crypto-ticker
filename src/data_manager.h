#pragma once
#include "ticker_types.h"

// Initialize with config and ticker data array
void initDataManager(AppConfig* config, TickerData* tickerData);

// Call this regularly from the fetch task (Core 0)
// Handles scheduling of all API fetches based on intervals
void updateData();

// Force an immediate refresh of all data
void forceRefresh();

// Get a string showing fetch status for debug
String getDataStatus();
