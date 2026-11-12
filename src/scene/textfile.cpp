#include "scene/textfile.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace photon {

bool TextFile::load(const std::string& p, std::string& err) {
    path = p;
    lines.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f) { err = "cannot open " + p; return false; }
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (n == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);  // UTF-8 byte order mark
        TextLine tl;
        tl.number = n;
        std::string cur;
        bool inQuote = false, have = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (inQuote) {
                if (c == '"') { inQuote = false; tl.tok.push_back(cur); cur.clear(); have = false; }
                else cur += c;
                continue;
            }
            if (c == '#') break;
            if (c == '"') { inQuote = true; continue; }
            if (c == ' ' || c == '\t' || c == '\r') {
                if (have) { tl.tok.push_back(cur); cur.clear(); have = false; }
                continue;
            }
            cur += c;
            have = true;
        }
        if (have) tl.tok.push_back(cur);
        if (inQuote) { err = p + ":" + std::to_string(n) + ": unterminated quote"; return false; }
        if (!tl.tok.empty()) lines.push_back(std::move(tl));
    }
    return true;
}

double toNumber(const std::string& s, bool& ok) {
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    ok = end && *end == 0 && !s.empty();
    return v;
}

} // namespace photon
