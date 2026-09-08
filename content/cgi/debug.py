#!/usr/bin/env python3

import os
import sys

print("Content-Type: text/html")
print()

print("<html><body>")
print("<h1>CGI Debug</h1>")

print("<h2>Environment</h2>")
for key in sorted(os.environ):
    if key.startswith(("REQUEST_", "QUERY_", "CONTENT_", "SCRIPT_", "SERVER_")):
        print("<p><b>" + key + "</b> = " +
              os.environ[key] + "</p>")

print("</body></html>")