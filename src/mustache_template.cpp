#include "mustache_template.h"
#include "mustache_template_provider.h"
#include "string_builder.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/array.hpp>

#include <array>

namespace {

godot::String stringify(const godot::Variant &var) {
	if (var) {
		return var.stringify();
	}
	return godot::String();
}

using ReplacePair = godot::Pair<char32_t, std::u32string_view>;

static const std::array<ReplacePair, 5> xml_tokens = {
	ReplacePair(U'&', U"&amp;"),
	ReplacePair(U'<', U"&lt;"),
	ReplacePair(U'>', U"&gt;"),
	ReplacePair(U'"', U"&quot;"),
	ReplacePair(U'\'', U"&apos;"),
};

int64_t compute_escape_size(std::u32string_view str) {
	int64_t size = 0;
	for (char32_t c : str) {
		bool replaced = false;
		for (const ReplacePair &pair : xml_tokens) {
			if (c == pair.first) {
				size += pair.second.size();
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			size += 1;
		}
	}
	return size;
}

godot::String escape_html(std::u32string_view input) {
	godot::Char32String output;
	output.resize(compute_escape_size(input) + 1);

	char32_t *buffer = output.ptrw();
	size_t pos = 0;
	for (char32_t c : input) {
		bool replaced = false;
		for (const ReplacePair &pair : xml_tokens) {
			if (c == pair.first) {
				for (char32_t r : pair.second) {
					buffer[pos++] = r;
				}
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			buffer[pos++] = c;
		}
	}

	return godot::String(output);
}

bool element_needs_name(MustacheElement::Type type) {
	switch (type) {
		case MustacheElement::Type::EscapedVariable:
		case MustacheElement::Type::RawVariable:
		case MustacheElement::Type::TripleMustache:
		case MustacheElement::Type::SectionBegin:
		case MustacheElement::Type::InvertedSectionBegin:
		case MustacheElement::Type::SectionEnd:
		case MustacheElement::Type::Partial:
			return true;
		default:
			return false;
	}
}

bool is_standalone_type(MustacheElement::Type type) {
	switch (type) {
		case MustacheElement::Type::Comment:
		case MustacheElement::Type::SectionBegin:
		case MustacheElement::Type::InvertedSectionBegin:
		case MustacheElement::Type::SectionEnd:
		case MustacheElement::Type::Partial:
		case MustacheElement::Type::Delimiter:
			return true;
		default:
			return false;
	}
}

bool is_whitespace(char32_t c) {
	return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\f' || c == U'\v';
}

std::u32string_view trim(std::u32string_view value) {
	size_t begin = 0;
	while (begin < value.size() && is_whitespace(value[begin])) {
		++begin;
	}
	if (begin == value.size()) {
		return std::u32string_view();
	}

	size_t end = value.size() - 1;
	while (end != std::u32string_view::npos && is_whitespace(value[end])) {
		if (end == 0) {
			return std::u32string_view();
		}
		--end;
	}

	return value.substr(begin, end - begin + 1);
}

uint64_t count_newlines(std::u32string_view value) {
	uint64_t count = 0;
	for (char32_t c : value) {
		if (c == U'\n') {
			++count;
		}
	}
	return count;
}

MustacheSize estimate_element_count(std::u32string_view buffer) {
	MustacheSize elements = 0;
	for (size_t i = 1; i < buffer.size(); ++i) {
		if (buffer[i - 1] == U'{' && i + 1 < buffer.size() && buffer[i] == U'{') {
			++elements;
		}
	}
	return elements;
}

struct U32StringViewHash {
	static _FORCE_INLINE_ uint32_t hash(std::u32string_view p_string) {
		uint32_t h = 5381;
		for (char32_t c : p_string) {
			h = ((h << 5) + h) ^ static_cast<uint32_t>(c);
		}
		return h;
	}
};

struct U32StringViewEqual {
	static _FORCE_INLINE_ bool compare(std::u32string_view p_lhs, std::u32string_view p_rhs) {
		return p_lhs == p_rhs;
	}
};

size_t add_segment(MustacheTemplate::Data &data, std::u32string_view name, godot::HashMap<std::u32string_view, size_t, U32StringViewHash, U32StringViewEqual> &name_to_segment_index) {
	auto itr = name_to_segment_index.find(name);
	if (itr) {
		return itr->value;
	}

	if (name == U".") {
		size_t segment_index = data.m_segments.size();
		size_t start_index = data.m_keys.size();
		data.m_keys.push_back(godot::StringName("."));
		data.m_segments.push_back(Segment(start_index, start_index + 1));
		name_to_segment_index.insert(name, segment_index);
		return segment_index;
	}

	size_t segment_index = data.m_segments.size();
	size_t start_index = data.m_keys.size();
	size_t begin = 0;
	while (begin <= name.size()) {
		size_t end = begin;
		while (end < name.size() && name[end] != U'.') {
			++end;
		}

		std::u32string_view part = name.substr(begin, end - begin);
		std::u32string part_string(part);
		data.m_keys.push_back(godot::StringName(part_string.c_str()));

		if (end == name.size()) {
			break;
		}
		begin = end + 1;
	}

	Segment segment(start_index, data.m_keys.size());
	data.m_segments.push_back(segment);
	name_to_segment_index.insert(name, segment_index);
	return segment_index;
}

struct ParseTagResult {
	MustacheElement::Type type = MustacheElement::Type::Invalid;
	std::u32string_view name_view;
	std::u32string_view prefix_view;
	std::u32string_view tag_open;
	std::u32string_view tag_close;
	size_t close_pos = 0;
	std::u32string_view directive;
};

godot::Error parse_tag(std::u32string_view buffer, size_t open_pos, std::u32string_view &open_delim, std::u32string_view &close_delim, ParseTagResult &result) {
	bool triple = false;
	result.tag_open = open_delim;
	result.tag_close = close_delim;
	if (open_delim == U"{{" && open_pos + 3 <= buffer.size() && buffer.substr(open_pos, 3) == U"{{{") {
		triple = true;
		result.tag_open = U"{{{";
		result.tag_close = U"}}}";
	}

	size_t content_start = open_pos + result.tag_open.size();
	size_t close_pos = buffer.find(result.tag_close, content_start);
	if (close_pos == std::u32string_view::npos) {
		return godot::ERR_PARSE_ERROR;
	}
	result.close_pos = close_pos;

	std::u32string_view raw_content = buffer.substr(content_start, close_pos - content_start);
	size_t raw_begin = 0;
	while (raw_begin < raw_content.size() && is_whitespace(raw_content[raw_begin])) {
		++raw_begin;
	}
	size_t raw_end = raw_content.size();
	while (raw_end > raw_begin && is_whitespace(raw_content[raw_end - 1])) {
		--raw_end;
	}
	result.directive = raw_content.substr(raw_begin, raw_end - raw_begin);

	if (result.directive.empty()) {
		return godot::ERR_PARSE_ERROR;
	}

	if (triple) {
		result.type = MustacheElement::Type::TripleMustache;
		result.name_view = trim(result.directive);
		return godot::OK;
	}

	char32_t marker = result.directive[0];
	switch (marker) {
		case U'!':
			result.type = MustacheElement::Type::Comment;
			result.name_view = std::u32string_view();
			break;
		case U'=': {
			std::u32string_view inner = trim(result.directive.substr(1));
			if (inner.size() < 3 || inner.back() != U'=') {
				return godot::ERR_PARSE_ERROR;
			}
			inner = trim(inner.substr(0, inner.size() - 1));
			size_t sep = inner.find(U' ');
			if (sep == std::u32string_view::npos) {
				return godot::ERR_PARSE_ERROR;
			}
			open_delim = trim(inner.substr(0, sep));
			close_delim = trim(inner.substr(sep + 1));
			result.type = MustacheElement::Type::Delimiter;
			break;
		}
		case U'#':
			result.type = MustacheElement::Type::SectionBegin;
			result.name_view = trim(result.directive.substr(1));
			break;
		case U'^':
			result.type = MustacheElement::Type::InvertedSectionBegin;
			result.name_view = trim(result.directive.substr(1));
			break;
		case U'/':
			result.type = MustacheElement::Type::SectionEnd;
			result.name_view = trim(result.directive.substr(1));
			break;
		case U'>':
			result.type = MustacheElement::Type::Partial;
			result.name_view = trim(result.directive.substr(1));
			result.prefix_view = std::u32string_view();
			break;
		case U'&':
			result.type = MustacheElement::Type::RawVariable;
			result.name_view = trim(result.directive.substr(1));
			break;
		default:
			result.type = MustacheElement::Type::EscapedVariable;
			result.name_view = trim(result.directive);
			break;
	}

	return godot::OK;
}

godot::Error parse(std::u32string_view buffer, MustacheTemplate::Data &data, const godot::Ref<MustacheTemplateProvider> &provider) {
	data.m_elements.clear();
	data.m_keys.clear();
	data.m_segments.clear();
	data.m_partials.clear();
	godot::HashMap<std::u32string_view, size_t, U32StringViewHash, U32StringViewEqual> segment_map;

	godot::HashMap<std::u32string_view, size_t, U32StringViewHash, U32StringViewEqual> partial_map;

	MustacheSize elements = estimate_element_count(buffer);
	data.m_elements.reserve(elements);
	data.m_keys.reserve(elements * 3);
	data.m_segments.reserve(elements);
	data.m_partials.reserve(elements);
	segment_map.reserve(elements);
	partial_map.reserve(elements);

	std::u32string_view open_delim = U"{{";
	std::u32string_view close_delim = U"}}";
	uint64_t line_index = 1;
	size_t pos = 0;
	std::vector<size_t> section_stack;
	section_stack.reserve(elements / 2);

	auto push_text = [&](std::u32string_view text) {
		if (text.empty()) {
			return;
		}
		MustacheElement elem(MustacheElement::Type::Text, text, line_index);
		data.m_elements.push_back(elem);
		line_index += count_newlines(text);
	};

	while (pos < buffer.size()) {
		size_t open_pos = buffer.find(open_delim, pos);
		if (open_pos == std::u32string_view::npos) {
			break;
		}

		ParseTagResult result;
		godot::Error err = parse_tag(buffer, open_pos, open_delim, close_delim, result);
		if (err != godot::OK) {
			return err;
		}

		if (element_needs_name(result.type) && result.name_view.empty()) {
			return godot::ERR_PARSE_ERROR;
		}

		// Check if standalone
		size_t line_start = open_pos;
		while (line_start > 0 && buffer[line_start - 1] != U'\n') {
			--line_start;
		}
		bool before_whitespace = true;
		for (size_t i = line_start; i < open_pos; ++i) {
			if (!is_whitespace(buffer[i])) {
				before_whitespace = false;
				break;
			}
		}
		size_t after_start = result.close_pos + result.tag_close.size();
		size_t line_end = after_start;
		while (line_end < buffer.size() && buffer[line_end] != U'\n') {
			++line_end;
		}
		if (line_end < buffer.size()) {
			++line_end; // include the newline
		}
		bool after_whitespace = true;
		for (size_t i = after_start; i < line_end; ++i) {
			if (buffer[i] == U'\n') {
				break;
			}
			if (!is_whitespace(buffer[i])) {
				after_whitespace = false;
				break;
			}
		}
		bool standalone = before_whitespace && after_whitespace;

		if (result.type == MustacheElement::Type::Partial) {
			if (standalone) {
				result.prefix_view = buffer.substr(line_start, open_pos - line_start);
			} else {
				result.prefix_view = std::u32string_view();
			}
		}

		bool is_standalone = is_standalone_type(result.type);

		if (standalone && is_standalone) {
			if (line_start > pos) {
				push_text(buffer.substr(pos, line_start - pos));
			}
		} else {
			if (open_pos > pos) {
				push_text(buffer.substr(pos, open_pos - pos));
			}
		}

		TokenData token_data(result.tag_open, result.directive, result.tag_close);
		size_t element_index = data.m_elements.size();

		if (result.type == MustacheElement::Type::Comment || result.type == MustacheElement::Type::Delimiter) {
			MustacheElement element(result.type, 0, token_data, line_index);
			data.m_elements.push_back(element);
		} else if (result.type == MustacheElement::Type::Partial) {
			std::u32string_view name = result.name_view;
			auto itr = partial_map.find(name);
			size_t partial_index;
			if (itr) {
				partial_index = itr->value;
			} else {
				std::u32string name_u32(name);
				godot::String name_str = godot::String(name_u32.c_str());
				godot::Ref<MustacheTemplate> partial = provider->get_template(name_str);
				if (partial.is_null()) {
					return godot::ERR_PARSE_ERROR;
				}
				partial_index = data.m_partials.size();
				data.m_partials.push_back(partial);
				partial_map.insert(name, partial_index);
			}
			MustacheElement element(partial_index, result.prefix_view, token_data, line_index);
			data.m_elements.push_back(element);
		} else {
			size_t key_index = add_segment(data, result.name_view, segment_map);

			if (result.type == MustacheElement::Type::SectionBegin || result.type == MustacheElement::Type::InvertedSectionBegin) {
				MustacheElement element(result.type, key_index, 0, token_data, line_index);
				data.m_elements.push_back(element);
				section_stack.push_back(element_index);
			} else if (result.type == MustacheElement::Type::SectionEnd) {
				MustacheElement element(result.type, key_index, 0, token_data, line_index);
				data.m_elements.push_back(element);

				if (section_stack.empty()) {
					return godot::ERR_PARSE_ERROR;
				}

				size_t begin_index = section_stack.back();
				section_stack.pop_back();
				size_t begin_key = data.m_elements[begin_index].m_section.m_segmentIndex;
				if (begin_key >= data.m_keys.size() || key_index >= data.m_keys.size()) {
					return godot::ERR_PARSE_ERROR;
				}
				if (data.m_keys[begin_key] != data.m_keys[key_index]) {
					return godot::ERR_PARSE_ERROR;
				}

				data.m_elements[begin_index].m_section.m_jumpIndex = element_index;
				data.m_elements[element_index].m_section.m_jumpIndex = begin_index;
			} else {
				MustacheElement element(result.type, key_index, token_data, line_index);
				data.m_elements.push_back(element);
			}
		}

		line_index += count_newlines(buffer.substr(open_pos, result.close_pos + result.tag_close.size() - open_pos));
		if (standalone && is_standalone) {
			pos = line_end;
		} else {
			pos = result.close_pos + result.tag_close.size();
		}
	}

	if (pos < buffer.size()) {
		push_text(buffer.substr(pos));
	}

	if (!section_stack.empty()) {
		return godot::ERR_PARSE_ERROR;
	}

	return godot::OK;
}

} //namespace

void MustacheTemplate::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("parse_path", "path", "partials"), &MustacheTemplate::parse_path, memnew(NullMustacheTemplateProvider));
	godot::ClassDB::bind_method(godot::D_METHOD("parse_string", "text", "partials"), &MustacheTemplate::parse_string, memnew(NullMustacheTemplateProvider));

	godot::ClassDB::bind_method(godot::D_METHOD("execute", "value"), &MustacheTemplate::execute);
}

godot::Error MustacheTemplate::parse_path(const godot::String &path, const godot::Ref<MustacheTemplateProvider> &partials) {
	if (!godot::FileAccess::file_exists(path)) {
		return godot::ERR_FILE_NOT_FOUND;
	}

	return parse_string(godot::FileAccess::get_file_as_string(path), partials);
}

godot::Error MustacheTemplate::parse_string(const godot::String &string, const godot::Ref<MustacheTemplateProvider> &partials) {
	m_buffer = string.utf32();
	return parse({ m_buffer.get_data(), static_cast<size_t>(m_buffer.length()) }, m_data, partials);
}

godot::String MustacheTemplate::execute(const godot::Variant &value) {
	class Variant {
	public:
		enum Type {
			VARIANT,
			ARRAY,
		};

		Variant() : m_type(VARIANT) {
		}

		Variant(const godot::Variant &v) : m_type(Type::VARIANT), m_variant(v) {
		}

		bool init_section() {
			if (m_variant.get_type() == godot::Variant::Type::ARRAY) {
				m_type = Type::ARRAY;
				m_index = 0;
				godot::Array array = m_variant;
				return !array.is_empty();
			}

			return m_variant;
		}

		godot::Variant get() const {
			switch (m_type) {
				case Type::VARIANT: {
					return m_variant;
				}
				case Type::ARRAY: {
					godot::Array arr = m_variant;
					if (m_index < arr.size()) {
						return arr[m_index];
					}
				}
			}

			return godot::Variant();
		}

		bool next() {
			switch (m_type) {
				case Type::VARIANT:
					return false;
				case Type::ARRAY: {
					godot::Array arr = m_variant;
					++m_index;
					return m_index < arr.size();
				}
			}

			return false;
		}

	private:
		Type m_type;
		godot::Variant m_variant;
		uint64_t m_index;
	};

	StringBuilder builder;
	godot::LocalVector<Variant, int64_t> context_stack;
	context_stack.reserve(m_data.m_elements.size() / 2);
	context_stack.push_back(value);
	size_t element_index = 0;

	auto resolve_key = [&](size_t segment_index) -> Variant {
		ERR_FAIL_COND_V(segment_index >= m_data.m_segments.size(), godot::Variant());
		auto segment = m_data.m_segments[segment_index];
		if ((segment.second - segment.first) == 1 && m_data.m_keys[segment.first] == godot::StringName(".")) {
			return Variant(context_stack[context_stack.size() - 1].get());
		}
		auto match_segment = [&](Variant &current) -> bool {
			for (size_t i = segment.first; i < segment.second; ++i) {
				ERR_FAIL_COND_V(i >= m_data.m_keys.size(), godot::Variant());
				if (current.get().get_type() != godot::Variant::Type::DICTIONARY) {
					current = Variant();
					return i != segment.first;
				}
				const godot::StringName &key = m_data.m_keys[i];
				godot::Dictionary dict = current.get();
				if (!dict.has(key)) {
					current = Variant();
					return i != segment.first;
				}
				current = Variant(dict[key]);
			}
			return true;
		};
		for (int64_t stack_idx = context_stack.size() - 1; stack_idx >= 0; --stack_idx) {
			Variant current = context_stack[stack_idx];
			if (match_segment(current)) {
				return current;
			}
		}
		return godot::Variant();
	};

	while (element_index < m_data.m_elements.size()) {
		const MustacheElement &elem = m_data.m_elements[element_index];
		switch (elem.m_type) {
			case MustacheElement::Type::Text:
				builder.append(elem.m_text.m_content);
				break;
			case MustacheElement::Type::EscapedVariable: {
				Variant resolved = resolve_key(elem.m_variable.m_segmentIndex);
				godot::String str = stringify(resolved.get());
				builder.append(escape_html({ str.ptr(), static_cast<size_t>(str.length()) }));
			} break;
			case MustacheElement::Type::RawVariable: {
				Variant resolved = resolve_key(elem.m_variable.m_segmentIndex);
				godot::String str = stringify(resolved.get());
				builder.append(str);
			} break;
			case MustacheElement::Type::TripleMustache: {
				Variant resolved = resolve_key(elem.m_variable.m_segmentIndex);
				godot::String str = stringify(resolved.get());
				builder.append(str);
			} break;
			case MustacheElement::Type::SectionBegin: {
				Variant resolved = resolve_key(elem.m_section.m_segmentIndex);
				if (resolved.init_section()) {
					context_stack.push_back(resolved);
				} else {
					element_index = elem.m_section.m_jumpIndex;
				}
			} break;
			case MustacheElement::Type::InvertedSectionBegin: {
				Variant resolved = resolve_key(elem.m_section.m_segmentIndex);
				if (resolved.get()) {
					element_index = elem.m_section.m_jumpIndex;
				} else {
					context_stack.push_back(resolved);
				}
			} break;
			case MustacheElement::Type::SectionEnd:
				if (context_stack[context_stack.size() - 1].next()) {
					element_index = elem.m_section.m_jumpIndex;
				} else {
					context_stack.remove_at(context_stack.size() - 1);
				}
				break;
			case MustacheElement::Type::Partial: {
				builder.append(elem.m_partial.m_prefix);
				const auto &partial = m_data.m_partials[elem.m_partial.m_partialIndex];
				if (partial.is_valid()) {
					godot::String result = partial->execute(context_stack[context_stack.size() - 1].get());
					builder.append(result);
				}
			} break;
			default:
				break;
		}
		++element_index;
	}

	return builder.as_string();
}
