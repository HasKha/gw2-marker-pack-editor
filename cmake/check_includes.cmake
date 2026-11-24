# Fails when anything under src/<FOLDER>/ includes a folder of src/ that is neither FOLDER nor
# one of ALLOWED (comma-separated). Run by ctest as <FOLDER>_layering, which markus_layering() in
# markus.cmake registers; the reason it exists is there.
string(REPLACE "," ";" allowed "${ALLOWED}")
list(APPEND allowed "${FOLDER}")

file(GLOB children LIST_DIRECTORIES true "${SRC_DIR}/*")
set(forbidden "")
foreach(child IN LISTS children)
    get_filename_component(name "${child}" NAME)
    if(IS_DIRECTORY "${child}" AND NOT name IN_LIST allowed)
        list(APPEND forbidden "${name}")
    endif()
endforeach()
if(NOT forbidden)
    return()
endif()
list(JOIN forbidden "|" forbidden_pattern)

file(GLOB_RECURSE sources "${SRC_DIR}/${FOLDER}/*.h" "${SRC_DIR}/${FOLDER}/*.cpp")
set(offenders "")
foreach(source IN LISTS sources)
    file(STRINGS "${source}" hits REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<](${forbidden_pattern})/")
    if(hits)
        list(APPEND offenders "${source}: ${hits}")
    endif()
endforeach()

if(offenders)
    list(JOIN forbidden ", " forbidden_list)
    list(JOIN offenders "\n  " report)
    message(FATAL_ERROR "src/${FOLDER}/ must not include ${forbidden_list}:\n  ${report}")
endif()
