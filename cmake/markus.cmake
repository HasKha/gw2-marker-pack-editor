include_guard()

# What every target of ours does the same way. Each folder under src/ and test/ is one target,
# defined by the CMakeLists.txt in it; these keep those files to what differs between them.

# Every .h and .cpp under the calling folder, into `target`. A folder is one target, so it is
# described by where its files are rather than by a list kept beside them; a new file needs a
# configure pass to be seen. Grouped in the IDE by their path from the repository root.
function(markus_target_sources target)
    file(GLOB_RECURSE sources "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    target_sources(${target} PRIVATE ${sources})
    source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "" FILES ${sources})
endfunction()

# A ctest case, `<folder>_layering`, that fails when anything in the calling folder of src/
# includes a folder of src/ other than itself and those named after ALLOWS.
#
# Linking already points one way, but every library sees all of src/ as its include root, so
# a header from above would still compile, and only fail, if at all, at a link that may not
# happen until the library is used on its own. Checking the source is what makes the layering
# a build failure rather than a convention. See docs/architecture.md#build-targets.
function(markus_layering)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "ALLOWS")
    get_filename_component(folder "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    list(JOIN arg_ALLOWS "," allowed)
    add_test(NAME ${folder}_layering
        COMMAND ${CMAKE_COMMAND}
            "-DSRC_DIR=${PROJECT_SOURCE_DIR}/src"
            "-DFOLDER=${folder}"
            "-DALLOWED=${allowed}"
            -P "${PROJECT_SOURCE_DIR}/cmake/check_includes.cmake")
endfunction()
