#pragma once
#include <vector>
#include <string>
#include <cstring>
#include <cstddef>

class Buffer {
public:
    Buffer() : _start(0) {}

    void append(const char* data, size_t len) {
        _data.insert(_data.end(), data, data + len);
    }

    void append(const std::string& s) {
        if (!s.empty()) append(s.data(), s.size());
    }

    size_t size()  const { return _data.size() - _start; }
    bool   empty() const { return size() == 0; }

    const char* data() const {
        if (_data.empty() || _start >= _data.size()) return NULL;
        return &_data[0] + _start;
    }

    std::string str() const {
        if (empty()) return "";
        return std::string(data(), size());
    }

    void consume(size_t n) {
        _start += n;
        if (_start > _data.size()) _start = _data.size();
        if (_start > 8192) {
            _data.erase(_data.begin(), _data.begin() + (std::ptrdiff_t)_start);
            _start = 0;
        }
    }

    void clear() { _data.clear(); _start = 0; }

    size_t find(const std::string& needle) const {
        if (size() < needle.size()) return std::string::npos;
        const char* d  = data();
        size_t      n  = size();
        size_t      nl = needle.size();
        for (size_t i = 0; i + nl <= n; i++) {
            if (memcmp(d + i, needle.data(), nl) == 0) return i;
        }
        return std::string::npos;
    }

private:
    std::vector<char> _data;
    size_t            _start;
};
