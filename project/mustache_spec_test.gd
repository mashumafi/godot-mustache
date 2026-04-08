extends TestGroup

func _ready() -> void:
	var json := FileAccess.get_file_as_string("res://mustache-spec/specs/{spec}.json".format({"spec": name}))
	var spec = JSON.parse_string(json)
	assert(spec is Dictionary)
	_create_tests_from_dict(spec)

	super._ready()

func _create_tests_from_dict(spec: Dictionary):
	assert("tests" in spec)
	_create_tests_from_array(spec["tests"])

func _fix_int(value: float) -> Variant:
	var rounded := roundf(value)
	if is_zero_approx(value - rounded):
		return int(rounded)
	return value

func _fix_ints(value) -> Variant:
	if value is Dictionary:
		for key in value.keys():
			var kv = value[key]
			value[key] = _fix_ints(kv)
	if value is Array:
		for i in range(value.size()):
			value[i] = _fix_ints(value[i])
	if value is float:
		return _fix_int(value)
	return value

func _create_tests_from_array(specs: Array):
	for spec in specs:
		var test := TestCallable.new()
		test.name = spec.name
		test.test = func() -> Array[Error]:
			var template := MustacheTemplate.new()
			var parse_string_error := template.parse_string(spec.template)
			if parse_string_error != OK:
				printerr("Parsing failed for ", test.name)
				return [parse_string_error]

			var result := template.execute(_fix_ints(spec.data))
			if result != spec.expected:
				printerr("Results do not match for ", test.name, " expected: '", spec.expected, "' actual: '", result, "'")
				return [ERR_INVALID_DATA]

			return [OK]
		tests.add_child(test)
