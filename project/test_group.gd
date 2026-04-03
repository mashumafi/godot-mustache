class_name TestGroup
extends Test

@onready var tests := %Tests

func _ready() -> void:
	super._ready()

	var run_button := _make_run_button()
	tests.add_child(run_button)
	tests.move_child(run_button, 0)

func _run() -> Array[Error]:
	var results : Array[Error] = []
	for test in _get_tests():
		results.append_array(test._run())
	return results

func _get_tests():
	return tests.get_children().filter(func(child): return child is Test)
