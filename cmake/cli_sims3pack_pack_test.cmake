# Synthetic pack → list → extract CLI round-trip (issue #58).
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED SXPE_BIN OR NOT DEFINED SYN_DIR OR NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "SXPE_BIN, SYN_DIR, WORK_DIR required")
endif()

set(SRC "${WORK_DIR}/pack-src")
set(OUT_PACK "${WORK_DIR}/packed.sims3pack")
set(EXTRACT1 "${WORK_DIR}/extract1")
set(EXTRACT2 "${WORK_DIR}/extract2")
file(REMOVE_RECURSE "${SRC}" "${EXTRACT1}" "${EXTRACT2}")
file(REMOVE "${OUT_PACK}")
file(MAKE_DIRECTORY "${SRC}" "${EXTRACT1}" "${EXTRACT2}")

execute_process(
  COMMAND "${SXPE_BIN}" sims3pack extract
          --path "${SYN_DIR}/minimal.sims3pack"
          --out-dir "${EXTRACT1}" --index 0 --force --format json
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "extract failed: ${out}\n${err}")
endif()

# Copy extracted .package into source folder for pack.
file(GLOB pkgs "${EXTRACT1}/*.package")
list(LENGTH pkgs npkgs)
if(npkgs LESS 1)
  message(FATAL_ERROR "no package extracted")
endif()
list(GET pkgs 0 first_pkg)
get_filename_component(pkg_name "${first_pkg}" NAME)
file(COPY "${first_pkg}" DESTINATION "${SRC}")

execute_process(
  COMMAND "${SXPE_BIN}" sims3pack pack
          --source-dir "${SRC}" --path "${OUT_PACK}"
          --display-name "CLI Pack" --package-id "cli-pack-1" --force --format json
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "pack failed: ${out}\n${err}")
endif()
if(NOT out MATCHES "\"ok\":true")
  message(FATAL_ERROR "pack envelope not ok: ${out}")
endif()

execute_process(
  COMMAND "${SXPE_BIN}" sims3pack list --path "${OUT_PACK}" --format json
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "\"ok\":true")
  message(FATAL_ERROR "list failed: ${out}\n${err}")
endif()
if(NOT out MATCHES "CLI Pack")
  message(FATAL_ERROR "list missing display name: ${out}")
endif()

execute_process(
  COMMAND "${SXPE_BIN}" sims3pack extract
          --path "${OUT_PACK}" --out-dir "${EXTRACT2}" --index 0 --force --format json
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "\"ok\":true")
  message(FATAL_ERROR "re-extract failed: ${out}\n${err}")
endif()

if(NOT EXISTS "${EXTRACT2}/${pkg_name}")
  message(FATAL_ERROR "re-extracted package missing: ${EXTRACT2}/${pkg_name}")
endif()
message(STATUS "cli_sims3pack_pack round-trip ok")
