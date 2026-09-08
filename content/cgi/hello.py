#!/usr/bin/env python3

import os
import sys

print("Content-Type: text/html")
print()

print("<html>")
print("<body>")
print("<h1>Hello from Python CGI!</h1>")
print("<p>Method: " + os.environ.get("REQUEST_METHOD", "") + "</p>")
print("<p>Query: " + os.environ.get("QUERY_STRING", "") + "</p>")
print("</body>")
print("</html>")