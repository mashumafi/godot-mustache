#include "mustache_template.h"
#include "mustache_template_provider.h"
#include "string_builder.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/templates/a_hash_map.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/array.hpp>

#include <array>

namespace {

constexpr MustacheSize DOT_INDEX = -1;
constexpr MustacheSize IDENTIFIER_ERROR = -2;

_FORCE_INLINE_ godot::String stringify(const godot::Variant &var) {
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

_FORCE_INLINE_ bool get_replacement(std::u32string_view &replacement, char32_t c) {
	for (const ReplacePair &pair : xml_tokens) {
		if (c == pair.first) {
			replacement = pair.second;
			return true;
		}
	}
	return false;
}

_FORCE_INLINE_ int64_t compute_escape_size(std::u32string_view str) {
	int64_t size = 0;
	for (char32_t c : str) {
		std::u32string_view replacement;
		if (get_replacement(replacement, c)) {
			size += replacement.size();
		} else {
			size += 1;
		}
	}
	return size;
}

_FORCE_INLINE_ godot::String to_string(std::u32string_view input) {
	godot::String output;
	output.resize(input.size() + 1);

	char32_t *buffer = output.ptrw();
	size_t pos = 0;
	for (char32_t c : input) {
		buffer[pos++] = c;
	}

	return godot::String(output);
}

_FORCE_INLINE_ godot::String escape_html(std::u32string_view input) {
	godot::String output;
	output.resize(compute_escape_size(input) + 1);

	char32_t *buffer = output.ptrw();
	size_t pos = 0;
	for (char32_t c : input) {
		std::u32string_view replacement;
		if (get_replacement(replacement, c)) {
			for (char32_t r : replacement) {
				buffer[pos++] = r;
			}
		} else {
			buffer[pos++] = c;
		}
	}

	return output;
}

_FORCE_INLINE_ bool element_needs_name(MustacheElement::Type type) {
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

_FORCE_INLINE_ bool is_standalone_type(MustacheElement::Type type) {
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

static const std::u32string_view WHITESPACE = U" \t\n\r\f\v";

_FORCE_INLINE_ std::u32string_view ltrim(std::u32string_view value) {
	size_t begin = value.find_first_not_of(WHITESPACE);
	if (unlikely(begin == std::u32string_view::npos)) {
		return std::u32string_view();
	}

	return value.substr(begin);
}

_FORCE_INLINE_ std::u32string_view rtrim(std::u32string_view value) {
	size_t end = value.size() - 1;
	while (end != std::u32string_view::npos && WHITESPACE.find(value[end]) != std::u32string_view::npos) {
		--end;
	}
	if (unlikely(end == std::u32string_view::npos)) {
		return std::u32string_view();
	}

	return value.substr(0, end + 1);
}

_FORCE_INLINE_ std::u32string_view trim(std::u32string_view value) {
	return rtrim(ltrim(value));
}

_FORCE_INLINE_ uint64_t count_newlines(std::u32string_view value) {
	uint64_t count = 0;
	for (char32_t c : value) {
		if (c == U'\n') {
			++count;
		}
	}
	return count;
}

struct MustacheMetrics {
	MustacheSize tags = 0;
	MustacheSize partials = 0;
	MustacheSize keys = 0;
};

enum MetricState {
	OPEN_TAG,
	CLOSING_TAG,
	CLOSING_TAG_SKIP_KEYS,
};

godot::Error measure_metrics(std::u32string_view buffer, MustacheMetrics &metrics) {
	std::u32string_view open_tag = U"{{";
	std::u32string_view close_tag = U"}}";
	MetricState state = OPEN_TAG;

	auto length = [&]() -> size_t {
		switch (state) {
			case OPEN_TAG:
				return open_tag.size();
			case CLOSING_TAG:
				return close_tag.size();
			case CLOSING_TAG_SKIP_KEYS:
				return close_tag.size();
		}

		ERR_FAIL_V_MSG(0, "Undefined metric state.");
	};

	while (buffer.size() >= length()) {
		switch (state) {
			case OPEN_TAG: {
				if (buffer.substr(0, length()) == open_tag) {
					ERR_FAIL_COND_V_MSG(buffer.size() < 2, godot::Error::ERR_PARSE_ERROR, "Not enough text remaining for find a closing delimiter.");

					switch (buffer[2]) {
						case U'!':
							state = CLOSING_TAG_SKIP_KEYS;
							buffer.remove_prefix(3);
							break;
						case U'=':
							state = CLOSING_TAG_SKIP_KEYS; // TODO: Handle delimiter switching
							buffer.remove_prefix(3);
							break;
						case U'{':
							break; // TODO: Handle triple
						case U'/':
							state = CLOSING_TAG_SKIP_KEYS; // keys should already be added from begin section
							buffer.remove_prefix(3);
							break;
						case U'>':
							state = CLOSING_TAG_SKIP_KEYS; // partial does not use keys
							buffer.remove_prefix(3);
							break;
						default:
							state = CLOSING_TAG;
							buffer.remove_prefix(2);
							break;
					}
				}
				break;
			}
			case CLOSING_TAG: {
				if (buffer.substr(0, length()) == close_tag) {
					state = OPEN_TAG;
					buffer.remove_prefix(2);
				} else if (buffer[0] == U'.') {
					buffer.remove_prefix(1);
				}
				break;
			}
			case CLOSING_TAG_SKIP_KEYS: {
				if (buffer.substr(0, length()) == close_tag) {
					state = OPEN_TAG;
					buffer.remove_prefix(2);
				}
				break;
			}
		}
	}
	return godot::OK;
}

_FORCE_INLINE_ MustacheSize estimate_tag_count(std::u32string_view buffer) {
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

using U32StringViewHashMap = godot::AHashMap<std::u32string_view, size_t, U32StringViewHash, U32StringViewEqual>;

template <typename Factory>
class Cache {
public:
	_FORCE_INLINE_ Cache(godot::LocalVector<typename Factory::Type, MustacheSize> *p_cache) {
		cache = p_cache;
		cache->clear();
	}

	template <typename Provider>
	_FORCE_INLINE_ MustacheSize get(std::u32string_view name, Provider *provider) {
		auto itr = lookup.find(name);
		if (itr) {
			return itr->value;
		}

		typename Factory::Type result = Factory::make(name, *cache, provider);
		if (!unlikely(Factory::is_valid(result))) {
			return Factory::get_rc(result);
		}
		MustacheSize index = cache->size();
		cache->push_back(result);
		return lookup.insert(name, index)->value;
	}

	_FORCE_INLINE_ void reserve(uint32_t capacity) {
		lookup.reserve(capacity);
		cache->reserve(capacity);
	}

private:
	U32StringViewHashMap lookup;
	godot::LocalVector<typename Factory::Type, MustacheSize> *cache;
};

class SegmentFactory {
public:
	using Type = Segment;

	static _FORCE_INLINE_ Type make(std::u32string_view name, const godot::LocalVector<Segment, MustacheSize> &segments, godot::LocalVector<godot::StringName, MustacheSize> *keys) {
		if (name == U".") {
			return Segment(DOT_INDEX, 0);
		}

		size_t start_index = keys->size();
		while (!name.empty()) {
			size_t end = name.find(U'.');
			godot::String part = to_string(name.substr(0, end));
			ERR_FAIL_COND_V_MSG(!part.is_valid_identifier(), Segment(IDENTIFIER_ERROR, 0), "Invalid identifier.");

			keys->push_back(godot::StringName(part));

			if (end == std::u32string_view::npos) {
				break;
			}
			name.remove_prefix(end + 1);
		}

		return Segment(start_index, keys->size());
	}

	static bool is_valid(const Segment &segment) {
		return segment.second > 0;
	}

	static MustacheSize get_rc(const Segment &segment) {
		return segment.first;
	}
};

class PartialFactory {
public:
	using Type = godot::Ref<MustacheTemplate>;

	static _FORCE_INLINE_ Type make(std::u32string_view name, const godot::LocalVector<godot::Ref<MustacheTemplate>, MustacheSize> &partials, MustacheTemplateProvider *provider) {
		return provider->get_template(to_string(name));
	}

	static bool is_valid(const godot::Ref<MustacheTemplate> &tmpl) {
		return tmpl.is_valid();
	}

	static MustacheSize get_rc(const godot::Ref<MustacheTemplate> &tmpl) {
		return -1;
	}
};

struct ParseTagResult {
	MustacheElement::Type type = MustacheElement::Type::Invalid;
	std::u32string_view name_view;
	std::u32string_view tag_open;
	std::u32string_view tag_close;
	size_t close_pos;
	std::u32string_view directive;
};

_FORCE_INLINE_ godot::Error parse_delimiter(std::u32string_view directive, std::u32string_view &open_delim, std::u32string_view &close_delim) {
	DEV_ASSERT(!directive.empty());

	std::u32string_view inner = directive.substr(1);
	ERR_FAIL_COND_V_MSG(inner.size() < 3, godot::ERR_PARSE_ERROR, "No enough text to change the delimiter.");
	ERR_FAIL_COND_V_MSG(inner.back() != U'=', godot::ERR_PARSE_ERROR, "Expected = before closing delimiter.");
	inner = trim(inner.substr(0, inner.size() - 1));
	size_t sep = inner.find(U' ');
	ERR_FAIL_COND_V_MSG(sep == std::u32string_view::npos, godot::ERR_PARSE_ERROR, "Expected a space between the delimiters.");
	open_delim = inner.substr(0, sep);
	close_delim = ltrim(inner.substr(sep + 1));
	return godot::OK;
};

_FORCE_INLINE_ godot::Error parse_tag(std::u32string_view buffer, size_t open_pos, std::u32string_view &open_delim, std::u32string_view &close_delim, ParseTagResult &result) {
	bool triple = false;
	result.tag_open = open_delim;
	result.tag_close = close_delim;
	if (open_delim == U"{{" && open_pos + 3 <= buffer.size() && buffer.substr(open_pos, 3) == U"{{{") {
		triple = true;
		result.tag_open = U"{{{";
		result.tag_close = U"}}}";
	}

	size_t content_start = open_pos + result.tag_open.size();
	result.close_pos = buffer.find(result.tag_close, content_start);
	ERR_FAIL_COND_V_MSG(result.close_pos == std::u32string_view::npos, godot::ERR_PARSE_ERROR, "Missing closing tag delimiter.");

	result.directive = trim(buffer.substr(content_start, result.close_pos - content_start));

	ERR_FAIL_COND_V_MSG(result.directive.empty(), godot::ERR_PARSE_ERROR, "No text between delimiters.");

	if (triple) {
		result.type = MustacheElement::Type::TripleMustache;
		result.name_view = trim(result.directive);
		return godot::OK;
	}

	char32_t marker = result.directive[0];
	switch (marker) {
		case U'!':
			result.type = MustacheElement::Type::Comment;
			break;
		case U'=': {
			godot::Error parse_delimiter_result = parse_delimiter(result.directive, open_delim, close_delim);
			if (parse_delimiter_result != godot::OK) {
				return parse_delimiter_result;
			}
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

_FORCE_INLINE_ bool scan_standalone(size_t &line_start, size_t &line_end, std::u32string_view buffer) {
	size_t open_pos = line_start;
	size_t close_pos = line_end;

	while (line_start > 0 && buffer[line_start - 1] != U'\n') {
		--line_start;
	}
	bool has_whitespace_before = buffer.substr(line_start, open_pos - line_start).find_first_not_of(WHITESPACE) == std::u32string_view::npos;
	while (line_end < buffer.size() && buffer[line_end] != U'\n') {
		++line_end;
	}
	if (line_end < buffer.size()) {
		++line_end; // include the newline
	}
	bool has_whitespace_after = buffer.substr(close_pos, line_end - close_pos).find_first_not_of(WHITESPACE) == std::u32string_view::npos;
	return has_whitespace_before && has_whitespace_after;
}

_FORCE_INLINE_ bool is_begin_section(const MustacheElement &section) {
	switch (section.m_type) {
		case MustacheElement::Type::SectionBegin:
			return true;
		case MustacheElement::Type::InvertedSectionBegin:
			return true;
		default:
			return false;
	}
}

_FORCE_INLINE_ godot::Error parse(std::u32string_view buffer, MustacheTemplate::Data &data, const godot::Ref<MustacheTemplateProvider> &provider) {
	data.m_elements.clear();
	data.m_keys.clear();
	Cache<SegmentFactory> segment_cache(&data.m_segments);
	Cache<PartialFactory> partial_cache(&data.m_partials);

	MustacheSize tag_count = estimate_tag_count(buffer);
	MustacheSize tag_count_plus_text = tag_count * 2 + 1;
	data.m_elements.reserve(tag_count_plus_text);
	data.m_keys.reserve(tag_count * 3);
	segment_cache.reserve(tag_count);
	partial_cache.reserve(tag_count);

	std::u32string_view open_delim = U"{{";
	std::u32string_view close_delim = U"}}";
	uint64_t line_index = 0;
	size_t pos = 0;
	// Using index incase the elements do get resized
	godot::LocalVector<size_t> section_stack;
	section_stack.reserve(tag_count / 2);

	auto push_text = [&data, &line_index](std::u32string_view text) {
		if (text.empty()) {
			return;
		}
		data.m_elements.push_back(MustacheElement(MustacheElement::Type::Text, text, line_index));
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

		ERR_FAIL_COND_V_MSG(element_needs_name(result.type) && result.name_view.empty(), godot::ERR_PARSE_ERROR, "Tag type requires a name.");

		size_t line_start = open_pos;
		size_t line_end = result.close_pos + result.tag_close.size();
		bool is_surrounded_by_whitespace = scan_standalone(line_start, line_end, buffer);

		std::u32string_view prefix_view = U"";
		if (result.type == MustacheElement::Type::Partial && is_surrounded_by_whitespace) {
			prefix_view = buffer.substr(line_start, open_pos - line_start);
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

		if (result.type == MustacheElement::Type::Comment || result.type == MustacheElement::Type::Delimiter) {
			data.m_elements.push_back(MustacheElement(result.type, token_data, line_index));
		} else if (result.type == MustacheElement::Type::Partial) {
			size_t partial_index = partial_cache.get(result.name_view, provider.ptr());
			ERR_FAIL_COND_V_MSG(partial_index < 0, godot::ERR_PARSE_ERROR, "Could not find partial.");
			data.m_elements.push_back(MustacheElement(partial_index, prefix_view, token_data, line_index));
		} else {
			size_t element_index = data.m_elements.size();
			MustacheSize key_index = segment_cache.get(result.name_view, &data.m_keys);
			if (unlikely(key_index == IDENTIFIER_ERROR)) {
				return godot::ERR_PARSE_ERROR;
			}
			if (result.type == MustacheElement::Type::SectionBegin || result.type == MustacheElement::Type::InvertedSectionBegin) {
				data.m_elements.push_back(MustacheElement(result.type, key_index, -1, token_data, line_index));
				section_stack.push_back(element_index);
			} else if (result.type == MustacheElement::Type::SectionEnd) {
				ERR_FAIL_COND_V_MSG(section_stack.is_empty(), godot::ERR_PARSE_ERROR, "Encountered a closing tag with no opening tags.");

				const uint32_t section_back = section_stack.size() - 1;
				const size_t begin_index = section_stack[section_back];
				section_stack.remove_at(section_back);

				MustacheElement &begin_section = data.m_elements[begin_index];
				ERR_FAIL_COND_V(!is_begin_section(begin_section), godot::ERR_BUG);
				ERR_FAIL_COND_V_MSG(begin_section.m_section.m_segmentIndex != key_index, godot::ERR_PARSE_ERROR, "Closing tag does not match opening tag.");

				data.m_elements.push_back(MustacheElement(result.type, key_index, begin_index, token_data, line_index));
				begin_section.m_section.m_jumpIndex = element_index;
			} else {
				data.m_elements.push_back(MustacheElement(result.type, key_index, token_data, line_index));
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

	ERR_FAIL_COND_V_MSG(!section_stack.is_empty(), godot::ERR_PARSE_ERROR, "Not enough closing tags.");

	return godot::OK;
}

class MustacheValue {
public:
	enum Type {
		VARIANT,
		ARRAY,
	};

	explicit MustacheValue() : m_type(VARIANT) {
	}

	explicit MustacheValue(const godot::Variant &v) : m_type(Type::VARIANT), m_variant(v) {
	}

	_FORCE_INLINE_ bool init_section(const godot::LocalVector<MustacheValue, int64_t> &context_stack) {
		if (m_variant.get_type() == godot::Variant::Type::ARRAY) {
			m_type = Type::ARRAY;
			m_index = 0;
			// Empty array is falsey
		}

		if (!m_variant) {
			return false;
		}

		if (unlikely(detect_cycle(context_stack))) {
			return false;
		}

		return true;
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
	_FORCE_INLINE_ bool detect_cycle(const godot::LocalVector<MustacheValue, int64_t> &context_stack) {
		// to prevent a cycles, make sure this dictionary was never encountered before
		// false positives can occur when two dictionaries are identical but not referencing the same data
		const godot::Variant &value = get();
		if (value.get_type() == godot::Variant::DICTIONARY) {
			godot::Dictionary left = value;
			// going reverse since cycle will likely be at the end
			for (int64_t data_idx = context_stack.size() - 1; data_idx >= 0; --data_idx) {
				const godot::Variant &other = context_stack[data_idx].get();
				if (unlikely(other.get_type() != godot::Variant::DICTIONARY)) {
					continue;
				}
				godot::Dictionary right = other;
				// shallow comparison
				int64_t starting_depth = MAX_RECURSION - 3;
				if (unlikely(left.recursive_equal(right, starting_depth))) {
					return true;
				}
			}
		}
		return false;
	};

	Type m_type;
	godot::Variant m_variant;
	uint64_t m_index;
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
	m_buffer = string;
	godot::Error error = parse({ m_buffer.ptr(), static_cast<size_t>(m_buffer.length()) }, m_data, partials);
	if (error != godot::OK) {
		m_buffer = "";
		m_data.m_elements.clear();
		m_data.m_keys.clear();
		m_data.m_segments.clear();
		m_data.m_partials.clear();
	}
	return error;
}

godot::String MustacheTemplate::execute(const godot::Variant &value) {
	const size_t expected_partial_recursion_depth = 8;

	// TODO: Calculate actual partial line count too
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
	context_stack.push_back(MustacheValue(value));

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
	data_stack.push_back({ &m_data, 0, godot::String() });

	while (!data_stack.is_empty()) {
		DataSpan *current_data = &data_stack[data_stack.size() - 1];

		auto resolve_key = [&](size_t segment_index) -> MustacheValue {
			if (segment_index == DOT_INDEX) {
				return MustacheValue(context_stack[context_stack.size() - 1].get());
			}
			const Segment &segment = current_data->data->m_segments[segment_index];
			auto match_segment = [&](MustacheValue &current) -> bool {
				for (size_t i = segment.first; i < segment.second; ++i) {
					if (current.get().get_type() != godot::Variant::Type::DICTIONARY) {
						current = MustacheValue();
						return i != segment.first;
					}
					const godot::StringName &key = current_data->data->m_keys[i];
					godot::Dictionary dict = current.get();
					godot::Variant value = dict.get(key, godot::Variant());
					if (value.get_type() == godot::Variant::NIL) {
						current = MustacheValue();
						return i != segment.first;
					}
					current = MustacheValue(value);
				}
				return true;
			};
			// Search up the stack for the key
			for (int64_t context_idx = context_stack.size() - 1; context_idx >= 0; --context_idx) {
				MustacheValue current_context = context_stack[context_idx];
				if (match_segment(current_context)) {
					return current_context;
				}
			}
			return MustacheValue();
		};

		while (current_data->index < current_data->data->m_elements.size()) {
			const MustacheElement &elem = current_data->data->m_elements[current_data->index];
			switch (elem.m_type) {
				case MustacheElement::Type::Text: {
					builder.append_with_prefix(::to_string(elem.m_text.m_content), current_data->prefix);
					break;
				}
				case MustacheElement::Type::EscapedVariable: {
					MustacheValue resolved = resolve_key(elem.m_variable.m_segmentIndex);
					godot::String str = stringify(resolved.get());
					godot::String escaped = escape_html({ str.ptr(), static_cast<size_t>(str.length()) });
					builder.append_with_prefix(escaped, current_data->prefix);
				} break;
				case MustacheElement::Type::RawVariable: {
					MustacheValue resolved = resolve_key(elem.m_variable.m_segmentIndex);
					godot::String str = stringify(resolved.get());
					builder.append_with_prefix(str, current_data->prefix);
				} break;
				case MustacheElement::Type::TripleMustache: {
					MustacheValue resolved = resolve_key(elem.m_variable.m_segmentIndex);
					godot::String str = stringify(resolved.get());
					builder.append_with_prefix(str, current_data->prefix);
				} break;
				case MustacheElement::Type::SectionBegin: {
					MustacheValue resolved = resolve_key(elem.m_section.m_segmentIndex);
					if (resolved.init_section(context_stack)) {
						context_stack.push_back(resolved);
					} else {
						current_data->index = elem.m_section.m_jumpIndex;
					}
				} break;
				case MustacheElement::Type::InvertedSectionBegin: {
					MustacheValue resolved = resolve_key(elem.m_section.m_segmentIndex);
					if (resolved.get()) {
						current_data->index = elem.m_section.m_jumpIndex;
					} else {
						context_stack.push_back(resolved);
					}
				} break;
				case MustacheElement::Type::SectionEnd: {
					const MustacheElement &begin_section = current_data->data->m_elements[elem.m_section.m_jumpIndex];
					DEV_ASSERT(is_begin_section(begin_section));
					DEV_ASSERT(begin_section.m_section.m_jumpIndex == current_data->index);
					DEV_ASSERT(begin_section.m_section.m_segmentIndex == elem.m_section.m_segmentIndex);

					if (context_stack[context_stack.size() - 1].next()) {
						current_data->index = elem.m_section.m_jumpIndex;
					} else {
						context_stack.remove_at(context_stack.size() - 1);
					}
					break;
				}
				case MustacheElement::Type::Partial: {
					godot::Ref<MustacheTemplate> partial = current_data->data->m_partials[elem.m_partial.m_partialIndex];
					DEV_ASSERT(likely(partial.is_valid()));

					++current_data->index;
					data_stack.push_back({ &partial->m_data, 0, current_data->prefix + ::to_string(elem.m_partial.m_prefix) }); // TODO: Create and concat string in one allocation
					current_data = &data_stack[data_stack.size() - 1];
					builder.append(current_data->prefix);
					continue;
				} break;
				default:
					break;
			}
			++current_data->index;
		}
		data_stack.remove_at(data_stack.size() - 1);
	}

	return builder.as_string();
}
