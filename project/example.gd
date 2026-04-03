extends Node


func _ready() -> void:
	var example := MustacheTemplate.new()
	var parse_path_rc := example.parse_path("./example.mustache")
	assert(parse_path_rc == OK)
	var result := example.execute({
		%"test": "world"
	})
	prints("Result: ", result)
