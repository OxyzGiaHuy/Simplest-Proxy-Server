#include "../include/utils.h"
#include "../include/config.h"
#include <iostream>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <regex>
#include <winuser.h>
#include <iomanip>

std::string getLogFileName()
{
    std::time_t now = std::time(nullptr);
    std::tm *localTime = std::localtime(&now);
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "log-%d-%m-%Y.txt", localTime);

    return std::string(buffer);
}

void logMessageToFile(const std::string &message)
{
    std::string logFileName = getLogFileName();

    std::fstream logFile(logFileName, std::ios::app);
    if (!logFile.is_open())
    {
        std::cerr << "Failed to open the log file: " << logFileName << "\n";
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm *localTime = std::localtime(&now);

    char timeBuffer[20];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", localTime);
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        logFile << "[" << timeBuffer << "] " << message << "\n";
    }

    logFile.close();
}

void logMessage(const std::string &message)
{
    std::time_t now = std::time(nullptr);
    std::tm *localTime = std::localtime(&now);

    char timeBuffer[20];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S %d/%m/%Y", localTime);
    std::string currentTime(timeBuffer);
    std::string tmp;
    tmp = "[" + currentTime + "] " + message + "\r\n";

    List.push(tmp);

    while (List.size() > MAX_LOG_ENTRIES)
    {
        List.pop();
    }

    std::string logContent;
    std::queue<std::string> tempQueue = List;
    while (!tempQueue.empty())
    {
        logContent += tempQueue.front();
        tempQueue.pop();
    }

    SetWindowTextA(hWndEdit, logContent.c_str());

    int textLength = GetWindowTextLengthA(hWndEdit);
    SendMessageA(hWndEdit, EM_SETSEL, textLength, textLength);
    SendMessageA(hWndEdit, EM_SCROLLCARET, 0, 0);
}

void ClientBoxMessage()
{
    std::string content;
    for (auto client : Clients)
    {
        content += client + "\r\n";
    }
    SetWindowTextA(hWndClient, content.c_str());
    int textLength = GetWindowTextLengthA(hWndEdit);
    SendMessageA(hWndClient, EM_SETSEL, textLength, textLength);
    SendMessageA(hWndClient, EM_SCROLLCARET, 0, 0);
}

// Helper function to extract hostname and port from the "Host" header.
bool parseHostHeader(const std::string &request, std::string &hostname, int &port)
{
    std::string hostHeaderLower;
    std::string::size_type pos = request.find("host:");
    if (pos == std::string::npos)
    {
        pos = request.find("Host:");
        if (pos == std::string::npos)
        {
            return false;
        }
    }

    pos += 5; // Move past "Host:" or "host:"
    std::string::size_type endline = request.find("\r\n", pos);
    if (endline == std::string::npos)
    {
        return false;
    }

    hostHeaderLower = request.substr(pos, endline - pos);
    size_t start = hostHeaderLower.find_first_not_of(' ');
    if (start != std::string::npos)
    {
        hostHeaderLower = hostHeaderLower.substr(start);
    }

    size_t colonPos = hostHeaderLower.find(":");
    if (colonPos == std::string::npos)
    {
        hostname = hostHeaderLower;
        port = 80;
    }
    else
    {
        hostname = hostHeaderLower.substr(0, colonPos);
        try
        {
            port = std::stoi(hostHeaderLower.substr(colonPos + 1));
        }
        catch (const std::invalid_argument &e)
        {
            // Invalid port, use default
            port = 80;
        }
    }
    return true;
}
// Function to resolve hostname to IP address
bool resolve_hostname(const char *hostname, struct sockaddr_in &server)
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, NULL, &hints, &res) == 0)
    {
        struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
        server.sin_addr = addr->sin_addr;
        char *ip_str = inet_ntoa(server.sin_addr);
        std::string s(hostname);
        std::string t(ip_str);
        // logMessage(s + " --> " + t + "\n");
        logMessageToFile(s + " --> " + t + "\n");
        freeaddrinfo(res);
        return true;
    }
    else
    {
        std::string s(hostname);
        logMessage("Failed to resolve hostname: " + s + "\n");
        logMessageToFile("Failed to resolve hostname: " + s + "\n");
        return false;
    }
}

// Function to check if a URL is in the blacklist
bool is_blacklisted(const std::string &hostname)
{
    std::lock_guard<std::mutex> lock(blacklist_mutex); // Đảm bảo thread-safe
    for (const auto &blocked : blacklist)
    {
        if (hostname.find(blocked) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

void add_to_blacklist(const std::string &url)
{
    std::lock_guard<std::mutex> lock(blacklist_mutex);

    std::string hostname;
    int port = 443; // Default port if no port is specified

    // Regular expression to match protocol, hostname, and optional port.
    std::regex url_regex(R"(^(?:(http|https):\/\/)?([^:\/]+)(?::(\d+))?(\/.*)?$)");
    std::smatch url_match;

    if (std::regex_match(url, url_match, url_regex))
    {
        hostname = url_match[2].str();
    }
    else
    {
        hostname = url;
    }

    bool isDuplicate = false;
    for (const auto &blocked : blacklist)
    {
        if (hostname == blocked)
        {
            isDuplicate = true;
            break;
        }
    }

    // Avoid duplicates
    if (!isDuplicate)
    {
        blacklist.push_back(hostname);
        std::string url_string = hostname;
        SendMessageA(hWndList, LB_ADDSTRING, 0, (LPARAM)url_string.c_str());
    }
}

// Function to remove a URL from the blacklist

void removeBlacklistUrl(int index)
{
    if (index >= 0 && index < blacklist.size())
    {
        std::lock_guard<std::mutex> lock(blacklist_mutex);
        blacklist.erase(blacklist.begin() + index);
        SendMessage(hWndList, LB_DELETESTRING, index, 0);
    }
}