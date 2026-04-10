#pragma once

#include "mustache_template.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/templates/hash_map.hpp>

class MustacheTemplateProvider : public godot::Resource {
	GDCLASS(MustacheTemplateProvider, godot::Resource);

public:
	void set_fallback(const godot::Ref<MustacheTemplateProvider> &p_fallback);
	virtual godot::Ref<MustacheTemplate> get_template(const godot::String &p_name);

protected:
	static void _bind_methods();

private:
	godot::Ref<MustacheTemplateProvider> fallback;
};

class ScriptableMustacheTemplateProvider : public MustacheTemplateProvider {
	GDCLASS(ScriptableMustacheTemplateProvider, MustacheTemplateProvider);

public:
	GDVIRTUAL1R(godot::Ref<MustacheTemplate>, _get_template, godot::String);

	virtual godot::Ref<MustacheTemplate> get_template(const godot::String &p_name) override;

protected:
	static void _bind_methods() {
		GDVIRTUAL_BIND(_get_template, "name");
	}
};

class DirMustacheTemplateProvider : public MustacheTemplateProvider {
	GDCLASS(DirMustacheTemplateProvider, MustacheTemplateProvider);

public:
	DirMustacheTemplateProvider();

	void set_path(const godot::String &p_path);
	godot::String get_path() const;

	void set_extension(const godot::String &p_extension);
	godot::String get_extension() const;

	void clear_cache();

	virtual godot::Ref<MustacheTemplate> get_template(const godot::String &p_name) override;

protected:
	static void _bind_methods();

private:
	godot::String path;
	godot::String extension;

	struct Cache {
		godot::Ref<MustacheTemplate> tmpl;
		uint64_t modified_time;
	};

	godot::HashMap<godot::String, Cache> cache;
};
