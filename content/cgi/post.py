#!/usr/bin/env python3

import os
import sys

content_length = int(os.environ.get("CONTENT_LENGTH", "0"))

body = sys.stdin.read(content_length)

print("Content-Type: text/html")
print()

print("<html>")
print("<body>")
print("<h1>Hello from Python CGI!</h1>")
print("<p>Method: " + os.environ.get("REQUEST_METHOD", "") + "</p>")
print("<p>Body: " + body + "</p>")
print("</body>")
print("</html>")