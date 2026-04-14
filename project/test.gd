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
		var start := Time.get_ticks_usec()
		var results = self._run()
		var end := Time.get_ticks_usec()
		prints("Testing", results.size(),  "tests took", (end - start) / 1000.0, "ms.")
		TestRunner.emit_test_results(results)
	)
	return run_button
