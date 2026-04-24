#include "Utils.h"

std::string get_str_last_n(const std::string& str, std::size_t n) {
    if (str.length() <= n) {
        return str;
    }
    return str.substr(str.length() - n);
}

String get_str_last_n(const String& str, std::size_t n) {
    const std::size_t len = str.length();
    if (len <= n) {
        return str;
    }
    return str.substring(len - n);
}