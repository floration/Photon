// Photon - line/token reader for the project's human-readable text formats.
// Each line is split into tokens; '#' starts a comment; "double quotes" group
// a token containing spaces.
#pragma once
#include <string>
#include <vector>

namespace photon {

struct TextLine {
    int number = 0;                  // 1-based source line
    std::vector<std::string> tok;
};

struct TextFile {
    std::string path;
    std::vector<TextLine> lines;
    bool load(const std::string& path, std::string& err);
};

double toNumber(const std::string& s, bool& ok);

} // namespace photon
