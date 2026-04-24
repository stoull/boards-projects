#pragma once

#include <Arduino.h>
#include <cstddef>
#include <string>

std::string get_str_last_n(const std::string& str, std::size_t n);
String get_str_last_n(const String& str, std::size_t n);
