/**************************************************************************/
/*  string_builder.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "string_builder.h"

void StringBuilder::reserve(uint32_t size) {
	strings.reserve(size);
	views.reserve(size);
}

StringBuilder &StringBuilder::append(const godot::String &p_string) {
	strings.push_back(p_string);
	return append({ p_string.ptr(), static_cast<size_t>(p_string.length()) });
}

StringBuilder &StringBuilder::append(std::u32string_view p_string) {
	if (p_string.empty()) {
		return *this;
	}

	views.push_back(p_string);

	string_length += p_string.size();

	return *this;
}

StringBuilder &StringBuilder::append_with_prefix(const godot::String &prefix, const godot::String &p_string, bool is_last) {
	// Hold the string to keep the buffer alive
	strings.push_back(p_string);

	return append_with_prefix(prefix, { p_string.ptr(), static_cast<size_t>(p_string.length()) }, is_last);
}

StringBuilder &StringBuilder::append_with_prefix(const godot::String &prefix, std::u32string_view p_string, bool is_last) {
	if (prefix.is_empty()) {
		return append(p_string);
	}

	for (size_t i = 0; i < p_string.size(); ++i) {
		if (p_string[i] == U'\n') {
			if (i + 1 == p_string.size() && is_last) {
				// Don't add a prefix if the string ends with a newline
				append(p_string);
				return *this;
			}
			append(p_string.substr(0, i + 1));
			append(prefix);
			p_string.remove_prefix(i + 1);
			i = 0;
		}
	}

	// Append any remaining text
	if (!p_string.empty()) {
		append(p_string);
	}

	return *this;
}

godot::String StringBuilder::as_string() const {
	if (string_length == 0) {
		return "";
	}

	godot::String string;
	string.resize(string_length + 1);
	char32_t *buffer = string.ptrw();

	size_t current_position = 0;

	for (std::u32string_view s : views) {
		size_t str_len = s.size();

		for (size_t j = 0; j < str_len; j++) {
			buffer[current_position + j] = s[j];
		}

		current_position += str_len;
	}
	buffer[current_position] = 0;

	return string;
}
