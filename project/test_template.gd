class_name TestTemplate
extends Test

@export var provider : MustacheTemplateProvider
@export var value : Variant
@export_multiline var expected : String

func _ready() -> void:
	super._ready()
	add_child(_make_run_button())

func _run() -> Array[Error]:
	var template = provider.get_template(name)
	assert(template)
	var result := template.execute(value)
	if expected != result:
		printerr("Result did not match ", result)
		return [ERR_INVALID_DATA]

	return [OK]
