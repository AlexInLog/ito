foreach(example sleep_sort downloads)
    add_executable(ito_example_${example} ${example}.cpp)
    target_link_libraries(ito_example_${example} PRIVATE ito)

    # Run as tests when the test suite is built, so the sanitizer jobs run the examples too; each one checks its own
    # outcome and exits non-zero if it's wrong.
    if(ITO_BUILD_TESTS)
        add_test(NAME example_${example} COMMAND ito_example_${example})
    endif()
endforeach()
