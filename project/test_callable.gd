class_name TestCallable
extends Test

var test : Callable = func():pass

func _ready() -> void:
	super._ready()
	add_child(_make_run_button())

func _run() -> Array[Error]:
	return test.call()
