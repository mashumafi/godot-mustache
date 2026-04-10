#include "mustache_template_provider.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/defs.hpp>

namespace {

uint64_t get_modified_time(const godot::String &filename) {
	if (godot::FileAccess::file_exists(filename)) {
		return godot::FileAccess::get_modified_time(filename);
	} else {
		godot::UtilityFunctions::printerr("Could not find file ", filename);
		godot::Time *time = godot::Time::get_singleton();
		godot::String now = time->get_time_string_from_system();
		return time->get_unix_time_from_datetime_string(now);
	}
}

} //namespace

void MustacheTemplateProvider::set_fallback(const godot::Ref<MustacheTemplateProvider> &p_fallback) {
	fallback = p_fallback;
}

godot::Ref<MustacheTemplate> MustacheTemplateProvider::get_template(const godot::String &p_name) {
	ERR_FAIL_COND_V(fallback.is_null(), nullptr);
	return fallback->get_template(p_name);
}

void MustacheTemplateProvider::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("set_fallback", "fallback"), &MustacheTemplateProvider::set_fallback);
	godot::ClassDB::bind_method(godot::D_METHOD("get_template", "name"), &MustacheTemplateProvider::get_template);
}

godot::Ref<MustacheTemplate> ScriptableMustacheTemplateProvider::get_template(const godot::String &p_name) {
	godot::Ref<MustacheTemplate> ret;
	if (!GDVIRTUAL_CALL(_get_template, p_name, ret)) {
		return godot::Ref<MustacheTemplate>();
	}

	if (ret.is_valid()) {
		return ret;
	}

	// fallback
	return MustacheTemplateProvider::get_template(p_name);
}

DirMustacheTemplateProvider::DirMustacheTemplateProvider() {
	path = "user://";
	extension = "mustache";
}

void DirMustacheTemplateProvider::set_path(const godot::String &p_path) {
	ERR_FAIL_COND_MSG(godot::DirAccess::open(p_path).is_null(), "Could not find dir");
	clear_cache();
	path = p_path;
}

godot::String DirMustacheTemplateProvider::get_path() const {
	return path;
}

void DirMustacheTemplateProvider::set_extension(const godot::String &p_extension) {
	clear_cache();
	extension = p_extension;
}

godot::String DirMustacheTemplateProvider::get_extension() const {
	return extension;
}

void DirMustacheTemplateProvider::clear_cache() {
	cache.clear();
}

godot::Ref<MustacheTemplate> DirMustacheTemplateProvider::get_template(const godot::String &p_name) {
	godot::String filename = path.path_join(p_name);
	if (!extension.is_empty()) {
		filename += "." + extension;
	}

	uint64_t modified_time = get_modified_time(filename);

	auto itr = cache.find(p_name);
	if (itr != cache.end()) {
		if (modified_time <= itr->value.modified_time) {
			return itr->value.tmpl;
		}
	}

	godot::Ref<MustacheTemplate> tmpl;
	tmpl.instantiate();

	// Allow recursion by inserting into the cache immediately
	auto inserted = cache.insert(p_name, { tmpl, modified_time });

	godot::Error parsed = tmpl->parse_path(filename, this);
	if (parsed != godot::OK) {
		cache.remove(inserted);

		// fallback
		return MustacheTemplateProvider::get_template(p_name);
	}
	return tmpl;
}

void DirMustacheTemplateProvider::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("set_path", "path"), &DirMustacheTemplateProvider::set_path);
	godot::ClassDB::bind_method(godot::D_METHOD("get_path"), &DirMustacheTemplateProvider::get_path);

	godot::ClassDB::bind_method(godot::D_METHOD("set_extension", "extension"), &DirMustacheTemplateProvider::set_extension);
	godot::ClassDB::bind_method(godot::D_METHOD("get_extension"), &DirMustacheTemplateProvider::get_extension);

	godot::ClassDB::bind_method(godot::D_METHOD("clear_cache"), &DirMustacheTemplateProvider::clear_cache);

	ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "path", godot::PropertyHint::PROPERTY_HINT_DIR, "", godot::PropertyUsageFlags::PROPERTY_USAGE_DEFAULT), "set_path", "get_path");
	ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "extension", godot::PropertyHint::PROPERTY_HINT_NONE, "", godot::PropertyUsageFlags::PROPERTY_USAGE_DEFAULT), "set_extension", "get_extension");
}
