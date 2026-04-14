#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <string_view>

class MustacheTemplateProvider;

using MustacheSize = uint64_t;
using Segment = godot::Pair<MustacheSize, MustacheSize>;

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

	inline MustacheElement() : m_type(Type::Invalid), m_line(0) {}

	inline MustacheElement(Type type, std::u32string_view text, uint64_t line) : m_type(type), m_text{ text }, m_line(line) {
	}
	inline MustacheElement(std::u32string_view text, uint64_t line) : MustacheElement(Type::Text, text, line) {
	}
	inline MustacheElement(Type type, const TokenData &data, uint64_t line) : m_type(type), m_tokenData{ data }, m_line(line) {
	}
	inline MustacheElement(Type type, MustacheSize segmentIndex, const TokenData &data, uint64_t line) : m_type(type), m_variable{ segmentIndex, data }, m_line(line) {
	}
	inline MustacheElement(Type type, MustacheSize segmentIndex, MustacheSize jumpIndex, const TokenData &data, uint64_t line) : m_type(type), m_section{ segmentIndex, std::u32string_view(), jumpIndex, data }, m_line(line) {
	}

	inline MustacheElement(MustacheSize partialIndex, std::u32string_view prefix, const TokenData &data, uint64_t line) : m_type(Type::Partial), m_partial{ partialIndex, prefix, data }, m_line(line) {
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
			MustacheSize m_segmentIndex;
			TokenData m_data;
		} m_variable;

		struct
		{
			MustacheSize m_partialIndex;
			std::u32string_view m_prefix; // white space before partial which the partial will be prefixed with
			TokenData m_data;
		} m_partial;

		struct
		{
			MustacheSize m_segmentIndex;
			std::u32string_view m_content;
			MustacheSize m_jumpIndex;
			TokenData m_data;
		} m_section;

		TokenData m_tokenData;
	};
};

class MustacheTemplate : public godot::RefCounted {
	GDCLASS(MustacheTemplate, godot::RefCounted)

public:
	struct Data {
		godot::LocalVector<MustacheElement, MustacheSize> m_elements;
		godot::LocalVector<godot::StringName, MustacheSize> m_keys;
		godot::LocalVector<Segment, MustacheSize> m_segments;
		godot::LocalVector<godot::Ref<MustacheTemplate>, MustacheSize> m_partials;
	};

	MustacheTemplate() = default;
	~MustacheTemplate() override = default;

	godot::Error parse_path(const godot::String &path, const godot::Ref<MustacheTemplateProvider> &partials);
	godot::Error parse_string(const godot::String &text, const godot::Ref<MustacheTemplateProvider> &partials);

	godot::String execute(const godot::Variant &value);

protected:
	static void _bind_methods();

private:
	godot::Ref<MustacheTemplateProvider> m_partialProvider;
	godot::String m_buffer;
	Data m_data;
};
