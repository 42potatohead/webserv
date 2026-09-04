#pragma once

#include <string>
#include <map>
#include <sstream>

struct HTTPRequest {
    std::string method;
    std::string uri;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;

    HTTPRequest() {}
};

class HTTPParser {
public:
    static bool parse(const std::string& raw, HTTPRequest& req) {
        size_t pos = 0;
        size_t end = raw.find("\r\n");
        if (end == std::string::npos) return false;

        // 1. Parse Request Line (e.g., "GET /kapouet HTTP/1.1")
        std::string requestLine = raw.substr(0, end);
        std::istringstream iss(requestLine);
        iss >> req.method >> req.uri >> req.version;

        pos = end + 2; // move past \r\n

        // 2. Parse Headers
        while ((end = raw.find("\r\n", pos)) != std::string::npos) {
            // An empty line (\r\n\r\n) means the end of the headers
            if (end == pos) {
                pos += 2;
                break;
            }

            std::string headerLine = raw.substr(pos, end - pos);
            size_t colon = headerLine.find(':');
            
            if (colon != std::string::npos) {
                std::string key = headerLine.substr(0, colon);
                std::string value = headerLine.substr(colon + 1);

                // Trim leading spaces from the value
                size_t start = value.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    value = value.substr(start);
                }
                
                req.headers[key] = value;
            }
            pos = end + 2;
        }

        // 3. Store whatever body is attached after the headers
        if (pos < raw.length()) {
            req.body = raw.substr(pos);
        }

        return true;
    }
};