#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <mutex>

// Structure to store blacklist information
struct BlacklistEntry
{
    std::string hostname;
    int port;
};

// Function prototypes
void addToBlacklist(const std::string &url);
bool isBlacklisted(const std::string &hostname);
void removeBlacklistEntry(int index);

// Global variables
extern std::vector<BlacklistEntry> blacklist;
extern std::mutex blacklist_mutex;
extern HWND hWndList;