#include "blacklist.h"
#include <iostream>
#include <windows.h>
#include <regex>
#include <mutex>

std::vector<BlacklistEntry> blacklist;
std::mutex blacklist_mutex;
extern HWND hWndList;

// Function to check if a URL is in the blacklist
bool isBlacklisted(const std::string &hostname)
{
    std::lock_guard<std::mutex> lock(blacklist_mutex); // Đảm bảo thread-safe
    for (const auto &blocked : blacklist)
    {
        if (hostname.find(blocked.hostname) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

void addToBlacklist(const std::string &url)
{
    std::lock_guard<std::mutex> lock(blacklist_mutex);

    std::string hostname;
    int port = 443; // Default port if no port is specified

    // Regular expression to match protocol, hostname, and optional port.
    std::regex url_regex(R"(^(?:(https?):\/\/)?([^:\/]+)(?::(\d+))?$)");
    std::smatch url_match;

    if (std::regex_match(url, url_match, url_regex))
    {
        std::string protocol = url_match[1].str();
        if (protocol == "http")
        {
            port = 80;
        }

        // Hostname (mandatory).
        hostname = url_match[2].str();
        // Port (optional).
        if (!url_match[3].str().empty())
        {
            try
            {
                port = std::stoi(url_match[3].str());
            }
            catch (const std::invalid_argument &e)
            {
                // Invalid port, use default
                port = 80;
            }
        }
    }
    else
    {
        // If the provided input doesn't match a URL format, consider it to be only host.
        hostname = url;
    }

    bool isDuplicate = false;
    for (const auto &blocked : blacklist)
    {
        if (hostname == blocked.hostname && port == blocked.port)
        {
            isDuplicate = true;
            break;
        }
    }

    // Avoid duplicates
    if (!isDuplicate)
    {
        BlacklistEntry entry = {hostname, port};
        blacklist.push_back(entry);
        std::string url_string = hostname;
        SendMessageA(hWndList, LB_ADDSTRING, 0, (LPARAM)url_string.c_str());
    }
}

// Function to remove a URL from the blacklist
void removeBlacklistEntry(int index)
{
    if (index >= 0 && index < blacklist.size())
    {
        std::lock_guard<std::mutex> lock(blacklist_mutex);
        blacklist.erase(blacklist.begin() + index);
        SendMessage(hWndList, LB_DELETESTRING, index, 0);
    }
}