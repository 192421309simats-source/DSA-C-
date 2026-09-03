#ifndef BASE64_H
#define BASE64_H

#include <string>
#include <vector>
#include <cstdint>

// Minimal Base64 decoder — used to turn the data-URL image payload the
// frontend sends (e.g. "data:image/jpeg;base64,/9j/4AAQ...") into raw bytes
// for stb_image to decode. No external dependency required.
namespace Base64 {

    inline std::vector<unsigned char> decode(const std::string& input) {
        static const std::string chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        auto isBase64 = [](unsigned char c) {
            return (isalnum(c) || c == '+' || c == '/');
        };

        std::vector<unsigned char> out;
        int val = 0, valb = -8;
        for (unsigned char c : input) {
            if (c == '=' || !isBase64(c)) {
                if (c == '=') break;
                continue; // skip whitespace/newlines
            }
            size_t pos = chars.find(static_cast<char>(c));
            if (pos == std::string::npos) continue;
            val = (val << 6) + static_cast<int>(pos);
            valb += 6;
            if (valb >= 0) {
                out.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    // Strips a "data:image/jpeg;base64," prefix if present, returning just
    // the base64 payload. Also returns the detected mime type via outMime.
    inline std::string stripDataUrlPrefix(const std::string& dataUrl, std::string& outMime) {
        outMime = "image/jpeg";
        size_t commaPos = dataUrl.find(',');
        if (dataUrl.substr(0, 5) == "data:" && commaPos != std::string::npos) {
            std::string header = dataUrl.substr(5, commaPos - 5); // "image/png;base64"
            size_t semi = header.find(';');
            if (semi != std::string::npos) outMime = header.substr(0, semi);
            return dataUrl.substr(commaPos + 1);
        }
        return dataUrl; // already raw base64
    }

    inline std::string mimeToExt(const std::string& mime) {
        if (mime.find("png") != std::string::npos) return ".png";
        if (mime.find("gif") != std::string::npos) return ".gif";
        if (mime.find("bmp") != std::string::npos) return ".bmp";
        return ".jpg";
    }
}

#endif // BASE64_H
