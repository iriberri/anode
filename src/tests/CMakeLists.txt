setup_llvm()

set(TESTS_SOURCE_FILES simple_tests.cpp catch.hpp test_util.h test_util.cpp lexer_tests.cpp ${ANODE_INCLUDE_DIR}/front/parse.h)

include_directories(${CMAKE_SOURCE_DIR}/externs/catch)

add_executable(simple_tests ${TESTS_SOURCE_FILES} )

target_link_libraries(simple_tests anode-back anode-front anode-execute ${LLVM_LIBS} ${LIB_GC})


add_test(NAME simple_tests COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/simple_tests)

file(GLOB test_suites "suites/*.an")
foreach(file ${test_suites})
    get_filename_component(test_name ${file} NAME_WE)
    add_test(
        NAME test-${test_name}
        COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/anode ${file})

endforeach()