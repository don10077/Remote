# Each run starts with no resources beside the executable, as on a clean CI runner.
# Keep the normal test executable and any manually edited resources untouched.
get_filename_component(testDirectory "${TEST_EXECUTABLE}" DIRECTORY)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef runId)
set(runDirectory "${testDirectory}/gui-test-${runId}")
file(MAKE_DIRECTORY "${runDirectory}/tools")
file(COPY "${TEST_EXECUTABLE}" DESTINATION "${runDirectory}")
file(COPY "${AUTO_ZONES_SCRIPT}" DESTINATION "${runDirectory}/tools")
get_filename_component(testName "${TEST_EXECUTABLE}" NAME)
execute_process(COMMAND "${runDirectory}/${testName}" RESULT_VARIABLE result)

# Only remove the fresh directory created by this invocation, below the test build directory.
file(REAL_PATH "${testDirectory}" testRoot)
file(REAL_PATH "${runDirectory}" runPath)
cmake_path(IS_PREFIX testRoot "${runPath}" NORMALIZE insideTestRoot)
if(NOT insideTestRoot OR runPath STREQUAL testRoot)
    message(FATAL_ERROR "GUI test cleanup path is outside the test build directory")
endif()
file(REMOVE_RECURSE "${runPath}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "GUI tests failed: ${result}")
endif()
