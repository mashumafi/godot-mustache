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

MustacheSize estimate_tag_count(std::u32string_view buffer) {
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

bool scan_standalone(size_t &line_start, size_t &line_end, std::u32string_view buffer, size_t after_start) {
	size_t open_pos = line_start;
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
	return before_whitespace && after_whitespace;
}

godot::Error parse(std::u32string_view buffer, MustacheTemplate::Data &data, const godot::Ref<MustacheTemplateProvider> &provider) {
	data.m_elements.clear();
	data.m_keys.clear();
	data.m_segments.clear();
	data.m_partials.clear();
	godot::HashMap<std::u32string_view, size_t, U32StringViewHash, U32StringViewEqual> segment_map;
	godot::HashMap<std::u32string_view, size_t, U32StringViewHash, U32StringViewEqual> partial_map;

	MustacheSize tag_count = estimate_tag_count(buffer);
	MustacheSize tag_count_plus_text = tag_count * 2 + 1;
	data.m_elements.reserve(tag_count_plus_text);
	data.m_keys.reserve(tag_count * 3);
	data.m_segments.reserve(tag_count);
	data.m_partials.reserve(tag_count);
	segment_map.reserve(tag_count);
	partial_map.reserve(tag_count);

	std::u32string_view open_delim = U"{{";
	std::u32string_view close_delim = U"}}";
	uint64_t line_index = 1;
	size_t pos = 0;
	godot::LocalVector<size_t> section_stack;
	section_stack.reserve(tag_count / 2);

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

		size_t line_start = open_pos;
		size_t after_start = result.close_pos + result.tag_close.size();
		size_t line_end = after_start;
		bool is_surrounded_by_whitespace = scan_standalone(line_start, line_end, buffer, after_start);

		if (result.type == MustacheElement::Type::Partial) {
			if (is_surrounded_by_whitespace) {
				result.prefix_view = buffer.substr(line_start, open_pos - line_start);
			} else {
				result.prefix_view = std::u32string_view();
			}
		}

		bool is_standalone = is_standalone_type(result.type);

		if (is_surrounded_by_whitespace && is_standalone) {
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
			MustacheElement element(result.type, token_data, line_index);
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

				if (section_stack.is_empty()) {
					return godot::ERR_PARSE_ERROR;
				}

				uint32_t section_back = section_stack.size() - 1;
				size_t begin_index = section_stack[section_back];
				section_stack.remove_at(section_back);
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
		if (is_surrounded_by_whitespace && is_standalone) {
			pos = line_end;
		} else {
			pos = result.close_pos + result.tag_close.size();
		}
	}

	if (pos < buffer.size()) {
		push_text(buffer.substr(pos));
	}

	if (!section_stack.is_empty()) {
		return godot::ERR_PARSE_ERROR;
	}

	return godot::OK;
}

class MustacheValue {
public:
	enum Type {
		VARIANT,
		ARRAY,
	};

	MustacheValue() : m_type(VARIANT) {
	}

	MustacheValue(const godot::Variant &v) : m_type(Type::VARIANT), m_variant(v) {
	}

	_FORCE_INLINE_ const godot::Variant &init_section() {
		if (m_variant.get_type() == godot::Variant::Type::ARRAY) {
			m_type = Type::ARRAY;
			m_index = 0;
			// Empty array is falsey
		}

		return m_variant;
	}

	_FORCE_INLINE_ godot::Variant get() const {
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

	_FORCE_INLINE_ bool next() {
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

_FORCE_INLINE_ bool detect_cycle(const godot::Variant &value, const godot::LocalVector<MustacheValue, int64_t> &context_stack) {
	// to prevent a cycles, make sure this dictionary was never encountered before
	// false positives can occur when two dictionaries are identical but not referencing the same data
	if (value.get_type() == godot::Variant::DICTIONARY) {
		godot::Dictionary left = value;
		for (int64_t data_idx = 0; data_idx < context_stack.size(); ++data_idx) {
			const godot::Variant &other = context_stack[data_idx].get();
			if (other.get_type() != godot::Variant::DICTIONARY) {
				continue;
			}
			godot::Dictionary right = other;
			// shallow comparison
			int64_t starting_depth = MAX_RECURSION - 3;
			if (left.recursive_equal(right, starting_depth)) {
				return true;
			}
		}
	}
	return false;
};

} //namespace

void MustacheTemplate::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("parse_path", "path", "partials"), &MustacheTemplate::parse_path, nullptr);
	godot::ClassDB::bind_method(godot::D_METHOD("parse_string", "text", "partials"), &MustacheTemplate::parse_string, nullptr);

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
	const size_t expected_partial_recursion_depth = 3;

	MustacheSize max_partial_element_count = 0;
	for (auto partial = m_data.m_partials.begin(); partial != m_data.m_partials.end(); ++partial) {
		// TODO: recursion using depth of `expected_partial_recursion_depth`
		max_partial_element_count = godot::MAX(max_partial_element_count, (*partial)->m_data.m_elements.size());
	}

	// Output text
	StringBuilder builder;
	// Each line in partial can result in appending 2 values to the buffer
	uint32_t partial_buffer_with_prefix = 2 * expected_partial_recursion_depth;
	builder.reserve(m_data.m_elements.size() + max_partial_element_count * partial_buffer_with_prefix);

	// Stack used for sections
	godot::LocalVector<MustacheValue, int64_t> context_stack;
	// Reserving half because sections take 2 tags (open/close)
	context_stack.reserve(m_data.m_elements.size() / 2 + max_partial_element_count * expected_partial_recursion_depth / 2);
	context_stack.push_back(value);

	// A span for iterating over a template
	// Used for the root template and all partials to avoid recursion
	struct DataSpan {
		Data *data;
		size_t index;
		godot::String prefix;
	};
	// Stack used for partials
	godot::LocalVector<DataSpan, int64_t> data_stack;
	uint32_t data_stack_estimate = 1 + (m_data.m_partials.is_empty() ? 0 : expected_partial_recursion_depth);
	data_stack.reserve(data_stack_estimate);
	data_stack.push_back({ &m_data, 0, "" });

	// Prefixes from partials
	StringBuilder prefixes;
	prefixes.reserve(data_stack_estimate);

	while (!data_stack.is_empty()) {
		DataSpan *current_data = &data_stack[data_stack.size() - 1];

		auto resolve_key = [&](size_t segment_index) -> MustacheValue {
			ERR_FAIL_COND_V(segment_index >= current_data->data->m_segments.size(), godot::Variant());
			auto segment = current_data->data->m_segments[segment_index];
			if ((segment.second - segment.first) == 1 && current_data->data->m_keys[segment.first] == godot::StringName(".")) {
				return MustacheValue(context_stack[context_stack.size() - 1].get());
			}
			auto match_segment = [&](MustacheValue &current) -> bool {
				for (size_t i = segment.first; i < segment.second; ++i) {
					ERR_FAIL_COND_V(i >= current_data->data->m_keys.size(), godot::Variant());
					if (current.get().get_type() != godot::Variant::Type::DICTIONARY) {
						current = MustacheValue();
						return i != segment.first;
					}
					const godot::StringName &key = current_data->data->m_keys[i];
					godot::Dictionary dict = current.get();
					if (!dict.has(key)) {
						current = MustacheValue();
						return i != segment.first;
					}
					current = MustacheValue(dict[key]);
				}
				return true;
			};
			for (int64_t context_idx = context_stack.size() - 1; context_idx >= 0; --context_idx) {
				MustacheValue current_context = context_stack[context_idx];
				if (match_segment(current_context)) {
					return current_context;
				}
			}
			return godot::Variant();
		};

		while (current_data->index < current_data->data->m_elements.size()) {
			const MustacheElement &elem = current_data->data->m_elements[current_data->index];
			switch (elem.m_type) {
				case MustacheElement::Type::Text:
					builder.append(elem.m_text.m_content);
					break;
				case MustacheElement::Type::EscapedVariable: {
					MustacheValue resolved = resolve_key(elem.m_variable.m_segmentIndex);
					godot::String str = stringify(resolved.get());
					builder.append(escape_html({ str.ptr(), static_cast<size_t>(str.length()) }));
				} break;
				case MustacheElement::Type::RawVariable: {
					MustacheValue resolved = resolve_key(elem.m_variable.m_segmentIndex);
					godot::String str = stringify(resolved.get());
					builder.append(str);
				} break;
				case MustacheElement::Type::TripleMustache: {
					MustacheValue resolved = resolve_key(elem.m_variable.m_segmentIndex);
					godot::String str = stringify(resolved.get());
					builder.append(str);
				} break;
				case MustacheElement::Type::SectionBegin: {
					MustacheValue resolved = resolve_key(elem.m_section.m_segmentIndex);
					const godot::Variant &value = resolved.init_section();
					if (value && !detect_cycle(value, context_stack)) {
						context_stack.push_back(resolved);
					} else {
						current_data->index = elem.m_section.m_jumpIndex;
					}
				} break;
				case MustacheElement::Type::InvertedSectionBegin: {
					MustacheValue resolved = resolve_key(elem.m_section.m_segmentIndex);
					const godot::Variant &value = resolved.get();
					if (value && !detect_cycle(value, context_stack)) {
						current_data->index = elem.m_section.m_jumpIndex;
					} else {
						context_stack.push_back(resolved);
					}
				} break;
				case MustacheElement::Type::SectionEnd:
					if (context_stack[context_stack.size() - 1].next()) {
						current_data->index = elem.m_section.m_jumpIndex;
					} else {
						context_stack.remove_at(context_stack.size() - 1);
					}
					break;
				case MustacheElement::Type::Partial: {
					godot::Ref<MustacheTemplate> partial = current_data->data->m_partials[elem.m_partial.m_partialIndex];
					if (partial.is_valid()) {
						++current_data->index;
						prefixes.append(elem.m_partial.m_prefix);
						data_stack.push_back({ &partial->m_data, 0, prefixes.as_string() });
						current_data = &data_stack[data_stack.size() - 1];
						builder.append(current_data->prefix); // TODO: Make this happen on all new lines
						continue;
					}
				} break;
				default:
					break;
			}
			++current_data->index;
		}
		prefixes.pop_back();
		data_stack.remove_at(data_stack.size() - 1);
	}

	return builder.as_string();
}
