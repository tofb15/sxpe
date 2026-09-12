# MCP stdio: exact Content-Length framing (no extra CRLF) + kebab tool names.
# Requests use JSON-line (also supported) so CMake file(WRITE) newline conversion
# on Windows cannot corrupt Content-Length input. The thing under test is stdout.
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED SXPE_MCP OR NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "SXPE_MCP and WORK_DIR required")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(IN "${WORK_DIR}/mcp-in.bin")
set(OUT "${WORK_DIR}/mcp-out.bin")

set(body1 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}")
set(body2 "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}")
file(WRITE "${IN}" "${body1}\n${body2}\n")

execute_process(
  COMMAND "${SXPE_MCP}"
  INPUT_FILE "${IN}"
  OUTPUT_FILE "${OUT}"
  RESULT_VARIABLE rc
  ERROR_VARIABLE err
  TIMEOUT 20)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "sxpe_mcp exit ${rc}: ${err}")
endif()

file(READ "${OUT}" raw HEX)
if(raw STREQUAL "")
  message(FATAL_ERROR "MCP stdout empty")
endif()
# Content-Length ASCII hex: 43 6f 6e 74 65 6e 74 2d 4c 65 6e 67 74 68 3a 20
string(FIND "${raw}" "436f6e74656e742d4c656e6774683a20" hdr)
if(hdr LESS 0)
  message(FATAL_ERROR "MCP stdout missing Content-Length header")
endif()
# Header separator is 0d 0a 0d 0a. Extra blank line would be 0d 0a 0d 0a 0d 0a.
string(FIND "${raw}" "0d0a0d0a0d0a" extra)
if(extra GREATER -1)
  message(FATAL_ERROR "MCP stdout has extra CRLF before the body")
endif()
string(FIND "${raw}" "0d0a0d0a7b" sep)  # \r\n\r\n{
if(sep LESS 0)
  message(FATAL_ERROR "MCP stdout is not Content-Length CRLF CRLF then JSON")
endif()

file(READ "${OUT}" text)
string(FIND "${text}" "resource_find-refs" kebab)
if(kebab LESS 0)
  message(FATAL_ERROR "tools/list missing kebab MCP name resource_find-refs")
endif()
string(FIND "${text}" "resource_findRefs" camel)
if(camel GREATER -1)
  message(FATAL_ERROR "tools/list still advertises camel MCP name resource_findRefs")
endif()
