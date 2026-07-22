#!/usr/bin/env python3
import os
import sys

method = os.environ.get("REQUEST_METHOD", "GET")
query  = os.environ.get("QUERY_STRING", "")

# Lire le corps si POST
body = ""
if method == "POST":
    length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    if length > 0:
        body = sys.stdin.read(length)

# Ecrire la reponse CGI (en-tetes puis corps)
print("Content-Type: text/html")
print("")  # ligne vide obligatoire = fin des en-tetes CGI

print("<!DOCTYPE html><html><body>")
print(f"<h1>Hello depuis CGI !</h1>")
print(f"<p>Methode : {method}</p>")
print(f"<p>Query string : {query}</p>")
if body:
    print(f"<p>Corps POST : {body}</p>")
print(f"<p>Python : {sys.version}</p>")
print("</body></html>")
