if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "Comic resource SOURCE_DIR is required")
endif()
if(NOT DEFINED DEST_DIR OR "${DEST_DIR}" STREQUAL "")
    message(FATAL_ERROR "Comic resource DEST_DIR is required")
endif()

if(NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "Comic source directory does not exist: ${SOURCE_DIR}")
endif()

set(source_manifest "${SOURCE_DIR}/manifest.json")
if(NOT EXISTS "${source_manifest}")
    message(FATAL_ERROR "Comic manifest does not exist: ${source_manifest}")
endif()

file(MAKE_DIRECTORY "${DEST_DIR}")
file(GLOB destination_entries LIST_DIRECTORIES true
    "${DEST_DIR}/*"
    "${DEST_DIR}/.*"
)
foreach(destination_entry IN LISTS destination_entries)
    file(REMOVE_RECURSE "${destination_entry}")
endforeach()

set(external_images)
file(GLOB source_entries LIST_DIRECTORIES false "${SOURCE_DIR}/*")
foreach(source_entry IN LISTS source_entries)
    get_filename_component(file_name "${source_entry}" NAME)
    string(SUBSTRING "${file_name}" 0 1 first_character)
    string(TOLOWER "${file_name}" lower_file_name)
    get_filename_component(file_extension "${file_name}" LAST_EXT)
    string(TOLOWER "${file_extension}" lower_extension)

    if(first_character STREQUAL "."
        OR lower_file_name MATCHES "(\\.tmp|\\.part|\\.bak)$"
        OR NOT lower_extension MATCHES "^\\.(jpg|jpeg|png)$"
        OR lower_file_name MATCHES "^comic_001\\.jpg$"
        OR lower_file_name MATCHES "^fallback\\.jpg$")
        continue()
    endif()

    list(APPEND external_images "${source_entry}")
endforeach()
list(SORT external_images)

file(COPY_FILE "${source_manifest}" "${DEST_DIR}/manifest.json")
foreach(source_entry IN LISTS external_images)
    get_filename_component(file_name "${source_entry}" NAME)
    file(COPY_FILE "${source_entry}" "${DEST_DIR}/${file_name}")
endforeach()

set(external_names)
foreach(source_entry IN LISTS external_images)
    get_filename_component(file_name "${source_entry}" NAME)
    list(APPEND external_names "${file_name}")
endforeach()

list(LENGTH external_images external_count)
message(STATUS "Comic source directory: ${SOURCE_DIR}")
message(STATUS "Comic destination directory: ${DEST_DIR}")
message(STATUS "Manifest: ${DEST_DIR}/manifest.json")
message(STATUS "External image count: ${external_count}")
if(external_names)
    message(STATUS "External image names: ${external_names}")
else()
    message(STATUS "External image names: <none>")
endif()
