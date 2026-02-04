

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)
#file(GLOB testFiles "*.cpp")
set(testFiles
	compile_test.cpp
	coro_dispatcher.cpp
	loader.cpp
	config_desc.cpp
	wandering_bst.cpp
	decimal.cpp
	cluster_alloc.cpp
	dispatcher.cpp
	stream_merge.cpp
	structured_ini.cpp
#	mq.cpp
	ta.cpp
	root.cpp
	integral.cpp
	pubsub.cpp
)

link_libraries(
     quarkbot_runtime
     quarkbot_mq
	${STANDARD_LIBRARIES}
)

foreach (testFile ${testFiles})
	string(REGEX MATCH "([^\/]+$)" filename ${testFile})
	string(REGEX MATCH "[^.]*" executable_name tests_${filename})
	add_executable(${executable_name} ${testFile})
	target_compile_definitions(${executable_name} PRIVATE
	   MODULE_PATH="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
	 )
	 target_link_libraries(${executable_name} quarkbot_impl)
	add_test(NAME "tests/${filename}" COMMAND ${executable_name})
endforeach ()

# target_compile_definitions(tests_coro_dispatcher PRIVATE BASIC_CORO_ENABLE_TRACE=1)