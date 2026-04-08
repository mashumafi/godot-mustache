#pragma once

#include "mustache_template.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

using namespace godot;

class MustacheTemplateProvider : public godot::RefCounted {
	GDCLASS(MustacheTemplateProvider, godot::RefCounted);

public:
	virtual godot::Ref<MustacheTemplate> get_template(godot::String name) = 0;

protected:
	static void _bind_methods() {
		godot::ClassDB::bind_method(godot::D_METHOD("get_template", "name"), &MustacheTemplateProvider::get_template);
	}
};

class ScriptableMustacheTemplateProvider : public MustacheTemplateProvider {
	GDCLASS(ScriptableMustacheTemplateProvider, MustacheTemplateProvider);

public:
	GDVIRTUAL1R(godot::Ref<MustacheTemplate>, _get_template, godot::String);

	virtual godot::Ref<MustacheTemplate> get_template(godot::String name) override {
		godot::Ref<MustacheTemplate> ret;
		if (GDVIRTUAL_CALL(_get_template, name, ret)) {
			return ret;
		}
		return godot::Ref<MustacheTemplate>();
	}

protected:
	static void _bind_methods() {
		GDVIRTUAL_BIND(_get_template, "name");
	}
};

class NullMustacheTemplateProvider : public MustacheTemplateProvider {
	GDCLASS(NullMustacheTemplateProvider, MustacheTemplateProvider);

public:
	virtual godot::Ref<MustacheTemplate> get_template(godot::String name) override {
		return memnew(MustacheTemplate);
	}

protected:
	static void _bind_methods() {
	}
};
