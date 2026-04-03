@abstract class_name Test
extends FoldableContainer

func _ready() -> void:
	title = name

func _run() -> Array[Error]:
	return []

func _make_run_button() -> Button:
	var run_button := Button.new()
	run_button.text = "Run"
	run_button.pressed.connect(func():
		TestRunner.emit_test_results(self._run())
	)
	return run_button
