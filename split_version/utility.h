#pragma once

#include <string>

bool parseHostHeader(const std::string &request, std::string &hostname, int &port);