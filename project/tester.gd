extends Control

func _ready() -> void:
	TestRunner.test_results.connect(_test_results)

func _test_results(results: Array[Error]) -> void:
	print(results)
