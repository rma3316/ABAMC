// SPDX-License-Identifier: GPL-3.0-only
#include "PackPreset.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <sstream>

namespace PackCode {
namespace {

constexpr int kMaxJsonBytes = 64 * 1024;
constexpr int kMaxCompressedBytes = 32 * 1024;
constexpr char kPrefix[] = "pc1.";

struct Sha256 {
    uint32_t state[8]{};
    uint64_t bits = 0;
    unsigned char buffer[64]{};
    size_t used = 0;

    Sha256()
    {
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
    }

    static uint32_t rotr(uint32_t value, uint32_t bits) { return (value >> bits) | (value << (32 - bits)); }

    void transform(const unsigned char* chunk)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(chunk[i * 4]) << 24) | (uint32_t(chunk[i * 4 + 1]) << 16) | (uint32_t(chunk[i * 4 + 2]) << 8) |
                   uint32_t(chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    void update(const unsigned char* data, size_t length)
    {
        bits += uint64_t(length) * 8;
        while (length > 0) {
            const size_t take = std::min(length, size_t(64 - used));
            std::memcpy(buffer + used, data, take);
            used += take;
            data += take;
            length -= take;
            if (used == 64) {
                transform(buffer);
                used = 0;
            }
        }
    }

    std::string hex8()
    {
        unsigned char block[64];
        std::memcpy(block, buffer, used);
        block[used++] = 0x80;
        if (used > 56) {
            while (used < 64)
                block[used++] = 0;
            transform(block);
            used = 0;
        }
        while (used < 56)
            block[used++] = 0;
        for (int i = 7; i >= 0; --i)
            block[used++] = static_cast<unsigned char>((bits >> (i * 8)) & 0xff);
        transform(block);
        std::string hex;
        hex.resize(8);
        const char* digits = "0123456789abcdef";
        const uint32_t first = state[0];
        for (int i = 0; i < 4; ++i) {
            const unsigned char byte = static_cast<unsigned char>((first >> ((3 - i) * 8)) & 0xff);
            hex[i * 2] = digits[byte >> 4];
            hex[i * 2 + 1] = digits[byte & 0x0f];
        }
        return hex;
    }
};

bool isAsciiLetter(char32_t ch)
{
    return (ch >= U'A' && ch <= U'Z') || (ch >= U'a' && ch <= U'z');
}

bool isAsciiDigit(char32_t ch)
{
    return ch >= U'0' && ch <= U'9';
}

std::string lowerAscii(const std::string& value)
{
    std::string out = value;
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

std::vector<char32_t> codePoints(const std::string& value)
{
    std::vector<char32_t> points;
    for (size_t i = 0; i < value.size();) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        char32_t cp = 0;
        size_t need = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i + 1 < value.size()) {
            need = 2;
            cp = ((c & 0x1f) << 6) | (static_cast<unsigned char>(value[i + 1]) & 0x3f);
        } else if ((c >> 4) == 0xe && i + 2 < value.size()) {
            need = 3;
            cp = ((c & 0x0f) << 12) | ((static_cast<unsigned char>(value[i + 1]) & 0x3f) << 6) |
                 (static_cast<unsigned char>(value[i + 2]) & 0x3f);
        } else if ((c >> 3) == 0x1e && i + 3 < value.size()) {
            need = 4;
            cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(value[i + 1]) & 0x3f) << 12) |
                 ((static_cast<unsigned char>(value[i + 2]) & 0x3f) << 6) | (static_cast<unsigned char>(value[i + 3]) & 0x3f);
        } else {
            throw Error("프리셋 JSON을 읽지 못했습니다.");
        }
        points.push_back(cp);
        i += need;
    }
    return points;
}

void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

std::string slug(const std::string& name)
{
    std::string chars;
    bool pendingDash = false;
    for (char32_t ch : codePoints(name)) {
        const bool keepAscii = isAsciiLetter(ch) || isAsciiDigit(ch) || ch == U'_' || ch == U'-';
        const bool keepOther = ch > 127;
        if (keepAscii || keepOther) {
            if (pendingDash && !chars.empty())
                chars.push_back('-');
            pendingDash = false;
            if (ch >= U'A' && ch <= U'Z')
                ch = ch - U'A' + U'a';
            appendUtf8(chars, ch);
        } else {
            pendingDash = true;
        }
    }
    while (!chars.empty() && chars.back() == '-')
        chars.pop_back();
    if (codePoints(chars).size() > 24) {
        std::string trimmed;
        size_t count = 0;
        for (char32_t ch : codePoints(chars)) {
            if (count == 24)
                break;
            appendUtf8(trimmed, ch);
            ++count;
        }
        while (!trimmed.empty() && trimmed.back() == '-')
            trimmed.pop_back();
        chars = trimmed;
    }
    return chars.empty() ? "pack" : chars;
}

bool isAsciiAlnum(unsigned char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool matchId(const std::string& value)
{
    if (value.empty() || value.size() > 64)
        return false;
    if (!isAsciiAlnum(static_cast<unsigned char>(value[0])))
        return false;
    for (unsigned char ch : value) {
        if (!(isAsciiAlnum(ch) || ch == '_' || ch == '-'))
            return false;
    }
    return true;
}

bool matchLoaderVersion(const std::string& value)
{
    if (value.empty() || value.size() > 32)
        return false;
    if (!isAsciiAlnum(static_cast<unsigned char>(value[0])))
        return false;
    for (unsigned char ch : value) {
        if (!(isAsciiAlnum(ch) || ch == '.' || ch == '+' || ch == '_' || ch == '-'))
            return false;
    }
    return true;
}

bool matchMinecraft(const std::string& value)
{
    int dots = 0;
    bool digit = false;
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '.') {
            if (!digit || dots == 2)
                return false;
            ++dots;
            digit = false;
            continue;
        }
        if (ch < '0' || ch > '9')
            return false;
        digit = true;
    }
    return digit && dots >= 1;
}

std::string trim(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(begin, end - begin);
}

std::string jsonString(const std::string& value)
{
    std::string out;
    out.push_back('"');
    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    const char* digits = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(digits[ch >> 4]);
                    out.push_back(digits[ch & 0x0f]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
        }
    }
    out.push_back('"');
    return out;
}

std::string base64Url(const unsigned char* data, size_t length)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    for (size_t i = 0; i < length; i += 3) {
        unsigned value = unsigned(data[i]) << 16;
        if (i + 1 < length)
            value |= unsigned(data[i + 1]) << 8;
        if (i + 2 < length)
            value |= unsigned(data[i + 2]);
        out.push_back(table[(value >> 18) & 63]);
        out.push_back(table[(value >> 12) & 63]);
        if (i + 1 < length)
            out.push_back(table[(value >> 6) & 63]);
        if (i + 2 < length)
            out.push_back(table[value & 63]);
    }
    return out;
}

int b64Value(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A';
    if (ch >= 'a' && ch <= 'z')
        return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9')
        return ch - '0' + 52;
    if (ch == '-')
        return 62;
    if (ch == '_')
        return 63;
    return -1;
}

std::vector<unsigned char> base64UrlDecode(const std::string& text)
{
    std::vector<unsigned char> out;
    unsigned value = 0;
    int bits = 0;
    for (char ch : text) {
        if (ch == '=')
            throw Error("코드를 읽지 못했습니다. 복사한 전체를 붙여 넣으세요.");
        const int digit = b64Value(ch);
        if (digit < 0)
            throw Error("코드를 읽지 못했습니다. 복사한 전체를 붙여 넣으세요.");
        value = (value << 6) | unsigned(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((value >> bits) & 0xff));
        }
    }
    return out;
}

std::vector<unsigned char> zlibCompress(const std::string& raw)
{
    uLong bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<unsigned char> out(bound);
    uLongf size = bound;
    const int result = compress2(out.data(), &size, reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), 9);
    if (result != Z_OK)
        throw Error("프리셋이 너무 큽니다.");
    out.resize(size);
    return out;
}

std::string zlibDecompress(const std::vector<unsigned char>& compressed)
{
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        throw Error("코드를 읽지 못했습니다. 복사한 전체를 붙여 넣으세요.");
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    std::string out;
    unsigned char buffer[8192];
    int result = Z_OK;
    while (result != Z_STREAM_END) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            throw Error("코드를 읽지 못했습니다. 복사한 전체를 붙여 넣으세요.");
        }
        const size_t got = sizeof(buffer) - stream.avail_out;
        if (out.size() + got > kMaxJsonBytes) {
            inflateEnd(&stream);
            throw Error("프리셋이 너무 큽니다.");
        }
        out.append(reinterpret_cast<char*>(buffer), got);
    }
    inflateEnd(&stream);
    if (out.empty())
        throw Error("코드를 읽지 못했습니다. 복사한 전체를 붙여 넣으세요.");
    return out;
}

class JsonCursor {
   public:
    explicit JsonCursor(const std::string& text) : text(text) {}

    void skip()
    {
        while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])))
            ++index;
    }

    char peek()
    {
        skip();
        if (index >= text.size())
            throw Error("프리셋 JSON을 읽지 못했습니다.");
        return text[index];
    }

    char take()
    {
        const char ch = peek();
        ++index;
        return ch;
    }

    void expect(char ch)
    {
        if (take() != ch)
            throw Error("프리셋 JSON을 읽지 못했습니다.");
    }

    std::string string()
    {
        expect('"');
        std::string out;
        while (index < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[index++]);
            if (ch == '"')
                return out;
            if (ch == '\\') {
                if (index >= text.size())
                    throw Error("프리셋 JSON을 읽지 못했습니다.");
                const char esc = text[index++];
                switch (esc) {
                    case '"':
                    case '\\':
                    case '/':
                        out.push_back(esc);
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'u': {
                        if (index + 4 > text.size())
                            throw Error("프리셋 JSON을 읽지 못했습니다.");
                        int code = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char hex = text[index++];
                            code <<= 4;
                            if (hex >= '0' && hex <= '9')
                                code += hex - '0';
                            else if (hex >= 'a' && hex <= 'f')
                                code += hex - 'a' + 10;
                            else if (hex >= 'A' && hex <= 'F')
                                code += hex - 'A' + 10;
                            else
                                throw Error("프리셋 JSON을 읽지 못했습니다.");
                        }
                        appendUtf8(out, static_cast<char32_t>(code));
                        break;
                    }
                    default:
                        throw Error("프리셋 JSON을 읽지 못했습니다.");
                }
            } else if (ch < 0x20) {
                throw Error("프리셋 JSON을 읽지 못했습니다.");
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
        throw Error("프리셋 JSON을 읽지 못했습니다.");
    }

    bool finished()
    {
        skip();
        return index >= text.size();
    }

   private:
    const std::string& text;
    size_t index = 0;
};

bool allowedKey(const std::string& key, std::initializer_list<const char*> allowed)
{
    for (const char* item : allowed) {
        if (key == item)
            return true;
    }
    return false;
}

void putU16(std::vector<unsigned char>& out, uint16_t value)
{
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(value & 0xff));
}

void putU32(std::vector<unsigned char>& out, uint32_t value)
{
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(value & 0xff));
}

void putNbtString(std::vector<unsigned char>& out, const std::string& value)
{
    if (value.size() > 65535)
        throw Error("서버 이름이 너무 깁니다.");
    putU16(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<unsigned char> gzip(const std::vector<unsigned char>& raw)
{
    z_stream stream{};
    if (deflateInit2(&stream, 9, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw Error("서버 목록 파일을 읽지 못했습니다.");
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(raw.data()));
    stream.avail_in = static_cast<uInt>(raw.size());
    std::vector<unsigned char> out;
    unsigned char buffer[4096];
    int result = Z_OK;
    while (result != Z_STREAM_END) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        result = deflate(&stream, Z_FINISH);
        if (result != Z_OK && result != Z_STREAM_END) {
            deflateEnd(&stream);
            throw Error("서버 목록 파일을 읽지 못했습니다.");
        }
        out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
    }
    deflateEnd(&stream);
    return out;
}

std::vector<unsigned char> gunzip(const std::vector<unsigned char>& compressed)
{
    z_stream stream{};
    if (inflateInit2(&stream, 15 + 16) != Z_OK)
        throw Error("서버 목록 파일을 읽지 못했습니다.");
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    std::vector<unsigned char> out;
    unsigned char buffer[4096];
    int result = Z_OK;
    while (result != Z_STREAM_END) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            throw Error("서버 목록 파일을 읽지 못했습니다.");
        }
        out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
    }
    inflateEnd(&stream);
    return out;
}

class NbtReader {
   public:
    explicit NbtReader(const std::vector<unsigned char>& data) : data(data) {}

    uint8_t u8()
    {
        if (index >= data.size())
            throw Error("서버 목록 파일이 잘렸습니다.");
        return data[index++];
    }

    uint16_t u16()
    {
        const uint16_t value = (uint16_t(u8()) << 8) | u8();
        return value;
    }

    int32_t i32()
    {
        const uint32_t value = (uint32_t(u8()) << 24) | (uint32_t(u8()) << 16) | (uint32_t(u8()) << 8) | u8();
        return static_cast<int32_t>(value);
    }

    std::string str()
    {
        const uint16_t length = u16();
        if (index + length > data.size())
            throw Error("서버 목록 파일이 잘렸습니다.");
        std::string value(reinterpret_cast<const char*>(data.data() + index), length);
        index += length;
        return value;
    }

    void skip(uint8_t tag)
    {
        switch (tag) {
            case 1:
                u8();
                return;
            case 2:
                u16();
                return;
            case 3:
            case 5:
                i32();
                return;
            case 4:
            case 6:
                i32();
                i32();
                return;
            case 8:
                str();
                return;
            case 10:
                while (true) {
                    const uint8_t child = u8();
                    if (child == 0)
                        return;
                    str();
                    skip(child);
                }
            default:
                throw Error("서버 목록 파일을 읽지 못했습니다.");
        }
    }

    bool done() const { return index >= data.size(); }

   private:
    const std::vector<unsigned char>& data;
    size_t index = 0;
};

}  // namespace

std::string loaderToJson(ModLoader loader)
{
    switch (loader) {
        case ModLoader::Fabric:
            return "fabric";
        case ModLoader::NeoForge:
            return "neoforge";
    }
    throw Error("로더를 알 수 없습니다.");
}

ModLoader loaderFromJson(const std::string& value)
{
    const std::string lower = lowerAscii(value);
    if (lower == "fabric")
        return ModLoader::Fabric;
    if (lower == "neoforge")
        return ModLoader::NeoForge;
    throw Error("로더는 fabric 또는 neoforge 입니다.");
}

std::string normalizeAddress(const std::string& address)
{
    const std::string trimmed = trim(address);
    if (trimmed.find("://") != std::string::npos || trimmed.find('/') != std::string::npos || trimmed.find(' ') != std::string::npos)
        throw Error("서버 주소는 '주소' 또는 '주소:포트' 형식이어야 합니다.");
    std::string host = trimmed;
    const auto colon = trimmed.rfind(':');
    if (colon != std::string::npos) {
        host = trimmed.substr(0, colon);
        const std::string portText = trimmed.substr(colon + 1);
        if (portText.empty() || !std::all_of(portText.begin(), portText.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
            throw Error("포트는 1부터 65535까지입니다.");
        int port = 0;
        for (char ch : portText) {
            port = port * 10 + (ch - '0');
            if (port > 65535)
                throw Error("포트는 1부터 65535까지입니다.");
        }
        if (port < 1)
            throw Error("포트는 1부터 65535까지입니다.");
    }
    if (host.empty() || host.size() > 253)
        throw Error("서버 주소가 비어 있거나 너무 깁니다.");
    return trimmed;
}

bool minecraftAtLeast121(const std::string& version)
{
    int major = 0;
    int minor = 0;
    std::stringstream stream(version);
    char dot = 0;
    stream >> major >> dot >> minor;
    return major > 1 || (major == 1 && minor >= 21);
}

Preset Preset::normalized() const
{
    const std::string nextName = trim(name);
    if (codePoints(nextName).size() < 1 || codePoints(nextName).size() > 40)
        throw Error("이름은 1자에서 40자까지입니다.");
    const std::string minecraft = trim(minecraftVersion);
    if (!matchMinecraft(minecraft))
        throw Error("마크 버전은 1.21.1 같은 형식이어야 합니다.");
    std::optional<std::string> nextLoader = loaderVersion;
    if (nextLoader && trim(*nextLoader).empty())
        nextLoader.reset();
    if (nextLoader) {
        nextLoader = trim(*nextLoader);
        if (!matchLoaderVersion(*nextLoader))
            throw Error("로더 버전 형식이 올바르지 않습니다.");
    }
    if (mods.size() > 100)
        throw Error("모드는 100개까지입니다.");
    std::vector<ModRef> nextMods;
    std::vector<std::string> seen;
    for (const ModRef& mod : mods) {
        const std::string project = trim(mod.projectId);
        if (!matchId(project))
            throw Error("모드 이름은 영문, 숫자, _, - 만 쓸 수 있습니다.");
        std::optional<std::string> version = mod.versionId;
        if (version && trim(*version).empty())
            version.reset();
        if (version) {
            version = trim(*version);
            if (!matchId(*version))
                throw Error("모드 버전 번호 형식이 올바르지 않습니다.");
        }
        const std::string key = lowerAscii(project);
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            throw Error("모드가 중복되었습니다: " + project);
        seen.push_back(key);
        nextMods.push_back(ModRef{project, version, mod.source});
    }
    std::sort(nextMods.begin(), nextMods.end(), [](const ModRef& left, const ModRef& right) {
        return lowerAscii(left.projectId) < lowerAscii(right.projectId);
    });
    std::optional<ServerRef> nextServer;
    if (server) {
        const std::string address = normalizeAddress(server->address);
        std::optional<std::string> serverName = server->name;
        if (serverName && trim(*serverName).empty())
            serverName.reset();
        if (serverName) {
            serverName = trim(*serverName);
            if (codePoints(*serverName).size() > 40)
                throw Error("서버 이름은 40자까지입니다.");
        }
        nextServer = ServerRef{address, serverName};
    }
    return Preset{nextName, minecraft, loader, nextMods, nextServer, nextLoader};
}

std::string canonicalJson(const Preset& preset)
{
    const Preset value = preset.normalized();
    std::string json = "{";
    json += "\"loader\":" + jsonString(loaderToJson(value.loader));
    if (value.loaderVersion)
        json += ",\"loader_version\":" + jsonString(*value.loaderVersion);
    json += ",\"mc_version\":" + jsonString(value.minecraftVersion);
    json += ",\"mods\":[";
    for (size_t i = 0; i < value.mods.size(); ++i) {
        if (i)
            json += ",";
        json += "{\"project_id\":" + jsonString(value.mods[i].projectId);
        if (value.mods[i].source == ModSource::CurseForge)
            json += ",\"source\":\"curseforge\"";
        if (value.mods[i].versionId)
            json += ",\"version_id\":" + jsonString(*value.mods[i].versionId);
        json += "}";
    }
    json += "],\"name\":" + jsonString(value.name);
    if (value.server) {
        json += ",\"server\":{\"address\":" + jsonString(value.server->address);
        if (value.server->name)
            json += ",\"name\":" + jsonString(*value.server->name);
        json += "}";
    }
    json += "}";
    return json;
}

Preset parsePresetJson(const std::string& json)
{
    JsonCursor cursor(json);
    if (cursor.peek() != '{')
        throw Error("프리셋은 JSON 객체여야 합니다.");
    cursor.expect('{');
    std::optional<std::string> name;
    std::optional<std::string> minecraft;
    std::optional<std::string> loader;
    std::optional<std::string> loaderVersion;
    std::optional<std::vector<ModRef>> mods;
    std::optional<ServerRef> server;
    bool sawServer = false;
    bool first = true;
    while (cursor.peek() != '}') {
        if (!first)
            cursor.expect(',');
        first = false;
        const std::string key = cursor.string();
        cursor.expect(':');
        if (!allowedKey(key, {"loader", "loader_version", "mc_version", "mods", "name", "server"}))
            throw Error("알 수 없는 항목이 있습니다: " + key);
        if (key == "mods") {
            if (cursor.peek() != '[')
                throw Error("모드 목록은 배열이어야 합니다.");
            cursor.expect('[');
            std::vector<ModRef> items;
            bool firstMod = true;
            while (cursor.peek() != ']') {
                if (!firstMod)
                    cursor.expect(',');
                firstMod = false;
                if (cursor.peek() != '{')
                    throw Error("모드 항목은 객체여야 합니다.");
                cursor.expect('{');
                std::optional<std::string> project;
                std::optional<std::string> version;
                ModSource source = ModSource::Modrinth;
                bool firstField = true;
                while (cursor.peek() != '}') {
                    if (!firstField)
                        cursor.expect(',');
                    firstField = false;
                    const std::string field = cursor.string();
                    cursor.expect(':');
                    if (!allowedKey(field, {"project_id", "source", "version_id"}))
                        throw Error("알 수 없는 항목이 있습니다: " + field);
                    if (cursor.peek() == 'n') {
                        cursor.expect('n');
                        cursor.expect('u');
                        cursor.expect('l');
                        cursor.expect('l');
                    } else if (field == "project_id") {
                        project = cursor.string();
                    } else if (field == "source") {
                        const std::string value = cursor.string();
                        if (value == "curseforge")
                            source = ModSource::CurseForge;
                        else if (value == "modrinth")
                            source = ModSource::Modrinth;
                        else
                            throw Error("모드 출처는 modrinth 또는 curseforge 입니다.");
                    } else {
                        version = cursor.string();
                    }
                }
                cursor.expect('}');
                if (!project)
                    throw Error("project_id 값이 없습니다.");
                items.push_back(ModRef{*project, version, source});
            }
            cursor.expect(']');
            mods = items;
        } else if (key == "server") {
            sawServer = true;
            if (cursor.peek() == 'n') {
                cursor.expect('n');
                cursor.expect('u');
                cursor.expect('l');
                cursor.expect('l');
            } else if (cursor.peek() == '{') {
                cursor.expect('{');
                std::optional<std::string> address;
                std::optional<std::string> serverName;
                bool firstField = true;
                while (cursor.peek() != '}') {
                    if (!firstField)
                        cursor.expect(',');
                    firstField = false;
                    const std::string field = cursor.string();
                    cursor.expect(':');
                    if (!allowedKey(field, {"address", "name"}))
                        throw Error("알 수 없는 항목이 있습니다: " + field);
                    if (cursor.peek() == 'n') {
                        cursor.expect('n');
                        cursor.expect('u');
                        cursor.expect('l');
                        cursor.expect('l');
                    } else if (field == "address") {
                        address = cursor.string();
                    } else {
                        serverName = cursor.string();
                    }
                }
                cursor.expect('}');
                if (!address)
                    throw Error("address 값이 없습니다.");
                server = ServerRef{*address, serverName};
            } else {
                throw Error("서버 정보는 객체여야 합니다.");
            }
        } else if (cursor.peek() != '"') {
            throw Error(key + " 값이 없습니다.");
        } else {
            const std::string text = cursor.string();
            if (key == "name")
                name = text;
            else if (key == "mc_version")
                minecraft = text;
            else if (key == "loader")
                loader = text;
            else
                loaderVersion = text;
        }
    }
    cursor.expect('}');
    if (!cursor.finished())
        throw Error("프리셋 JSON을 읽지 못했습니다.");
    if (!name)
        throw Error("name 값이 없습니다.");
    if (!minecraft)
        throw Error("mc_version 값이 없습니다.");
    if (!loader)
        throw Error("loader 값이 없습니다.");
    if (!mods)
        throw Error("모드 목록이 없습니다.");
    (void)sawServer;
    return Preset{*name, *minecraft, loaderFromJson(*loader), *mods, server, loaderVersion}.normalized();
}

std::string Preset::instanceDirectoryName() const
{
    const Preset value = normalized();
    const std::string json = canonicalJson(value);
    Sha256 hash;
    hash.update(reinterpret_cast<const unsigned char*>(json.data()), json.size());
    return slug(value.name) + "-" + hash.hex8();
}

std::string encode(const Preset& preset)
{
    const std::string json = canonicalJson(preset);
    if (json.size() > kMaxJsonBytes)
        throw Error("프리셋이 너무 큽니다.");
    const std::vector<unsigned char> compressed = zlibCompress(json);
    if (compressed.empty() || compressed.size() > kMaxCompressedBytes)
        throw Error("코드가 너무 깁니다.");
    return std::string(kPrefix) + base64Url(compressed.data(), compressed.size());
}

Preset decode(const std::string& code)
{
    std::string compact;
    compact.reserve(code.size());
    for (unsigned char ch : code) {
        if (!std::isspace(ch))
            compact.push_back(static_cast<char>(ch));
    }
    if (compact.rfind(kPrefix, 0) != 0)
        throw Error("코드는 pc1. 으로 시작해야 합니다.");
    const std::vector<unsigned char> compressed = base64UrlDecode(compact.substr(std::strlen(kPrefix)));
    if (compressed.empty() || compressed.size() > kMaxCompressedBytes)
        throw Error("코드가 너무 깁니다.");
    return parsePresetJson(zlibDecompress(compressed));
}

std::vector<unsigned char> writeServersDat(const std::vector<ServerRef>& servers)
{
    std::vector<unsigned char> raw;
    raw.push_back(10);
    putNbtString(raw, "");
    raw.push_back(9);
    putNbtString(raw, "servers");
    raw.push_back(10);
    putU32(raw, static_cast<uint32_t>(servers.size()));
    for (const ServerRef& server : servers) {
        raw.push_back(1);
        putNbtString(raw, "acceptTextures");
        raw.push_back(1);
        raw.push_back(1);
        putNbtString(raw, "hidden");
        raw.push_back(0);
        raw.push_back(8);
        putNbtString(raw, "ip");
        putNbtString(raw, server.address);
        raw.push_back(8);
        putNbtString(raw, "name");
        putNbtString(raw, server.name && !server.name->empty() ? *server.name : server.address);
        raw.push_back(0);
    }
    raw.push_back(0);
    return gzip(raw);
}

std::vector<ServerRef> readServersDat(const std::vector<unsigned char>& gzipped)
{
    const std::vector<unsigned char> raw = gunzip(gzipped);
    NbtReader reader(raw);
    if (reader.u8() != 10)
        throw Error("서버 목록 파일을 읽지 못했습니다.");
    reader.str();
    std::vector<ServerRef> servers;
    while (!reader.done()) {
        const uint8_t tag = reader.u8();
        if (tag == 0)
            break;
        const std::string name = reader.str();
        if (tag == 9 && name == "servers") {
            const uint8_t element = reader.u8();
            const int32_t count = reader.i32();
            if (element != 10)
                throw Error("서버 목록 파일을 읽지 못했습니다.");
            for (int32_t i = 0; i < count; ++i) {
                std::optional<std::string> address;
                std::optional<std::string> serverName;
                while (true) {
                    const uint8_t child = reader.u8();
                    if (child == 0)
                        break;
                    const std::string field = reader.str();
                    if (child == 8 && field == "ip")
                        address = reader.str();
                    else if (child == 8 && field == "name")
                        serverName = reader.str();
                    else
                        reader.skip(child);
                }
                if (!address || address->empty())
                    throw Error("서버 목록 파일을 읽지 못했습니다.");
                servers.push_back(ServerRef{*address, serverName});
            }
        } else {
            reader.skip(tag);
        }
    }
    return servers;
}

}  // namespace PackCode
