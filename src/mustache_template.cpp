#include "mustache_template.h"

#include <godot_cpp/classes/file_access.hpp>

namespace {

std::u32string_view next_line(std::u32string_view &text) {
	size_t index = text.find(U"\n");
	if (index == std::u32string_view::npos) {
		std::u32string_view result = text;
		text = U"";
		return result;
	}

	std::u32string_view result = text.substr(0, index);
	text = text.substr(index + 1);
	return result;
}

struct ElementCount {
	MustacheSize m_elements;
	MustacheSize m_lines;
};

ElementCount estimate_element_count(std::u32string_view buffer) {
	ElementCount count{ 0, 0 };
	for (size_t i = 0; i < buffer.size(); ++i) {
	}
	return count;
}

godot::Error parse(std::u32string_view buffer, MustacheTemplateData &data) {
	data.m_elements.clear();
	data.m_keys.clear();
	data.m_segments.clear();

	ElementCount count = estimate_element_count(buffer);
	data.m_elements.reserve(count.m_elements + count.m_lines);
	data.m_keys.reserve(count.m_elements * 3);
	data.m_segments.reserve(count.m_elements);

	uint64_t lineIndex = 0;
	while (!buffer.empty()) {
		std::u32string_view line = next_line(buffer);

		++lineIndex;
	}

	return godot::OK;
}

} //namespace

void MustacheTemplate::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("parse_path", "path"), &MustacheTemplate::parse_path);
	godot::ClassDB::bind_method(godot::D_METHOD("parse_string", "text"), &MustacheTemplate::parse_string);

	godot::ClassDB::bind_method(godot::D_METHOD("execute", "value"), &MustacheTemplate::execute);
}

godot::Error MustacheTemplate::parse_path(const godot::String &path) {
	if (!godot::FileAccess::file_exists(path)) {
		return godot::ERR_FILE_NOT_FOUND;
	}

	return parse_string(godot::FileAccess::get_file_as_string(path));
}

godot::Error MustacheTemplate::parse_string(const godot::String &string) {
	m_buffer = string.utf32();
	return parse({ m_buffer.get_data(), static_cast<size_t>(m_buffer.length()) }, m_data);
}

godot::String MustacheTemplate::execute(const godot::Variant &value) {
	return "";
}
