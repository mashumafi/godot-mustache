#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <string_view>

struct TokenData {
	inline TokenData(std::u32string_view beginDelimiter, std::u32string_view content, std::u32string_view endDelimiter) : m_beginDelimiter(beginDelimiter), m_content(content), m_endDelimiter(endDelimiter) {
	}

	std::u32string_view m_beginDelimiter;
	std::u32string_view m_content;
	std::u32string_view m_endDelimiter;
};

class MustacheElement {
public:
	enum class Type {
		EscapedVariable,
		RawVariable,
		SectionBegin,
		InvertedSectionBegin,
		SectionEnd,
		Partial,
		Text,

		Whitespace,
		TripleMustache,
		Comment,
		Delimiter,
		Invalid,
	};

	inline MustacheElement() : m_type(Type::Invalid) {}

	inline MustacheElement(Type type, std::u32string_view text) : m_type(type), m_text{ text } {
	}
	inline MustacheElement(std::u32string_view text) : MustacheElement(Type::Text, text) {
	}
	inline MustacheElement(Type type, size_t segmentIndex, const TokenData &data) : m_type(type), m_variable{ segmentIndex, data } {
	}
	inline MustacheElement(Type type, size_t segmentIndex, size_t jumpIndex, const TokenData &data) : m_type(type), m_section{ segmentIndex, std::u32string_view(), jumpIndex, data } {
	}

	inline MustacheElement(std::u32string_view name, std::u32string_view prefix, const TokenData &data) : m_type(Type::Partial), m_partial{ name, prefix, data } {
	}

	uint64_t m_line;

	Type m_type;
	union {
		struct
		{
			std::u32string_view m_content;
		} m_text;

		struct
		{
			size_t m_segmentIndex;
			TokenData m_data;
		} m_variable;

		struct
		{
			std::u32string_view m_name;
			std::u32string_view m_prefix; // white space before partial which the partial will be prefixed with
			TokenData m_data;
		} m_partial;

		struct
		{
			size_t m_segmentIndex;
			std::u32string_view m_content;
			size_t m_jumpIndex;
			TokenData m_data;
		} m_section;
	};
};

using Segment = godot::Pair<size_t, size_t>;

using MustacheSize = uint64_t;

struct MustacheTemplateData {
	godot::LocalVector<MustacheElement, MustacheSize> m_elements;
	godot::LocalVector<godot::StringName, MustacheSize> m_keys;
	godot::LocalVector<Segment, MustacheSize> m_segments;
};

class MustacheTemplate : public godot::RefCounted {
	GDCLASS(MustacheTemplate, godot::RefCounted)

public:
	MustacheTemplate() = default;
	~MustacheTemplate() override = default;

	godot::Error parse_path(const godot::String &path);
	godot::Error parse_string(const godot::String &text);

	godot::String execute(const godot::Variant &value);

protected:
	static void _bind_methods();

private:
	godot::Char32String m_buffer;
	MustacheTemplateData m_data;
};
