#pragma once
#include <string>

// Retourne le type MIME d'un fichier selon son extension
// Ex: "index.html" -> "text/html"
std::string getMimeType(const std::string& path);
