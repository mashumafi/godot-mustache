extends Node

signal test_results(results: Array[Error])

func emit_test_results(result: Array[Error]):
	test_results.emit(result)
