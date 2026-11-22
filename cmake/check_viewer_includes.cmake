# Fails when anything under src/viewer/ includes an editor header. Run by ctest as
# viewer_includes_no_editor, with -DVIEWER_DIR=<src/viewer>.
#
# The libraries already link one way — mpe_viewer never links mpe_editor — but both see all
# of src/ as their include root, so an editor header included from the viewer would still
# compile and only fail, if at all, at a link that may not happen until the viewer is used
# without the editor. Checking the source is what makes the rule a build failure rather than
# a convention. See docs/viewer.md#layout.
file(GLOB_RECURSE viewer_sources "${VIEWER_DIR}/*.h" "${VIEWER_DIR}/*.cpp")

set(offenders "")
foreach(source IN LISTS viewer_sources)
    file(STRINGS "${source}" hits REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<]editor/")
    if(hits)
        list(APPEND offenders "${source}: ${hits}")
    endif()
endforeach()

if(offenders)
    list(JOIN offenders "\n  " report)
    message(FATAL_ERROR "src/viewer/ must not include the editor:\n  ${report}")
endif()
