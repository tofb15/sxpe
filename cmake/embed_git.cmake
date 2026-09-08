# Writes sxpe/build_info.hpp: git branch/commit, compile UTC, original vs fork.
# Configure time + custom target so Help → About stays current.

if(NOT DEFINED SXPE_SOURCE_DIR OR NOT DEFINED SXPE_BUILD_INFO_IN OR NOT DEFINED SXPE_BUILD_INFO_OUT)
    message(FATAL_ERROR
        "embed_git.cmake needs SXPE_SOURCE_DIR, SXPE_BUILD_INFO_IN, SXPE_BUILD_INFO_OUT")
endif()

set(SXPE_GIT_CANONICAL_URL "https://github.com/tofb15/sxpe")
set(SXPE_GIT_BRANCH "unknown")
set(SXPE_GIT_COMMIT "unknown")
set(SXPE_GIT_DIRTY 0)
set(SXPE_GIT_LINEAGE "unknown")
set(SXPE_GIT_SOURCE_URL "")

string(TIMESTAMP SXPE_BUILD_UTC "%Y-%m-%d %H:%M UTC" UTC)

function(sxpe_github_owner_repo url out_owner out_repo)
    set(${out_owner} "" PARENT_SCOPE)
    set(${out_repo} "" PARENT_SCOPE)
    if(url STREQUAL "")
        return()
    endif()
    string(REPLACE "\\" "/" u "${url}")
    string(TOLOWER "${u}" u)
    string(REGEX REPLACE "\\.git/?$" "" u "${u}")
    string(REGEX REPLACE "/$" "" u "${u}")
    if(u MATCHES "github.com[:/]([^/]+)/([^/#?]+)")
        set(owner "${CMAKE_MATCH_1}")
        set(repo "${CMAKE_MATCH_2}")
        string(REGEX REPLACE "\\.git$" "" repo "${repo}")
        set(${out_owner} "${owner}" PARENT_SCOPE)
        set(${out_repo} "${repo}" PARENT_SCOPE)
    endif()
endfunction()

function(sxpe_classify_owner_repo owner repo out_lineage out_url)
    set(${out_lineage} "unknown" PARENT_SCOPE)
    set(${out_url} "" PARENT_SCOPE)
    if(owner STREQUAL "" OR repo STREQUAL "")
        return()
    endif()
    string(TOLOWER "${owner}" owner_lc)
    string(TOLOWER "${repo}" repo_lc)
    set(url "https://github.com/${owner_lc}/${repo_lc}")
    set(${out_url} "${url}" PARENT_SCOPE)
    if(owner_lc STREQUAL "tofb15" AND repo_lc STREQUAL "sxpe")
        set(${out_lineage} "official" PARENT_SCOPE)
    else()
        set(${out_lineage} "fork" PARENT_SCOPE)
    endif()
endfunction()

if(NOT GIT_EXECUTABLE)
    find_package(Git QUIET)
endif()
if(NOT GIT_EXECUTABLE)
    find_program(GIT_EXECUTABLE git)
endif()

set(_in_git FALSE)
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --is-inside-work-tree
        WORKING_DIRECTORY "${SXPE_SOURCE_DIR}"
        OUTPUT_VARIABLE _in_git_raw
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_in_git_raw STREQUAL "true")
        set(_in_git TRUE)
    endif()
endif()

if(_in_git)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" branch --show-current
        WORKING_DIRECTORY "${SXPE_SOURCE_DIR}"
        OUTPUT_VARIABLE SXPE_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${SXPE_SOURCE_DIR}"
        OUTPUT_VARIABLE SXPE_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${SXPE_SOURCE_DIR}"
        OUTPUT_VARIABLE _dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT _dirty STREQUAL "")
        set(SXPE_GIT_DIRTY 1)
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" remote
        WORKING_DIRECTORY "${SXPE_SOURCE_DIR}"
        OUTPUT_VARIABLE _remote_names
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    string(REPLACE "\n" ";" _remote_list "${_remote_names}")
    set(_origin_lin "")
    set(_origin_https "")
    set(_github_lin "")
    set(_github_https "")
    set(_any_official_https "")
    set(_any_fork_https "")
    foreach(_r IN LISTS _remote_list)
        if(_r STREQUAL "")
            continue()
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" remote get-url "${_r}"
            WORKING_DIRECTORY "${SXPE_SOURCE_DIR}"
            OUTPUT_VARIABLE _ru
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        sxpe_github_owner_repo("${_ru}" _ow _rp)
        sxpe_classify_owner_repo("${_ow}" "${_rp}" _lin _https)
        if(_lin STREQUAL "unknown")
            continue()
        endif()
        if(_r STREQUAL "origin")
            set(_origin_lin "${_lin}")
            set(_origin_https "${_https}")
        endif()
        if(_r STREQUAL "github")
            set(_github_lin "${_lin}")
            set(_github_https "${_https}")
        endif()
        if(_lin STREQUAL "official" AND _any_official_https STREQUAL "")
            set(_any_official_https "${_https}")
        endif()
        if(_lin STREQUAL "fork" AND _any_fork_https STREQUAL "")
            set(_any_fork_https "${_https}")
        endif()
    endforeach()
    # GitHub origin wins (a fork with upstream added is still a fork).
    # Else remote named github, else any official, else any other GitHub remote.
    if(NOT _origin_lin STREQUAL "")
        set(SXPE_GIT_LINEAGE "${_origin_lin}")
        set(SXPE_GIT_SOURCE_URL "${_origin_https}")
    elseif(NOT _github_lin STREQUAL "")
        set(SXPE_GIT_LINEAGE "${_github_lin}")
        set(SXPE_GIT_SOURCE_URL "${_github_https}")
    elseif(NOT _any_official_https STREQUAL "")
        set(SXPE_GIT_LINEAGE "official")
        set(SXPE_GIT_SOURCE_URL "${_any_official_https}")
    elseif(NOT _any_fork_https STREQUAL "")
        set(SXPE_GIT_LINEAGE "fork")
        set(SXPE_GIT_SOURCE_URL "${_any_fork_https}")
    endif()
endif()

# GitHub Actions: workflow repo wins (origin is that clone; fork PRs on
# tofb15/sxpe CI still count as official).
if(DEFINED ENV{GITHUB_REPOSITORY} AND NOT "$ENV{GITHUB_REPOSITORY}" STREQUAL "")
    string(REPLACE "/" ";" _gh_parts "$ENV{GITHUB_REPOSITORY}")
    list(LENGTH _gh_parts _gh_n)
    if(_gh_n GREATER_EQUAL 2)
        list(GET _gh_parts 0 _gh_owner)
        list(GET _gh_parts 1 _gh_repo)
        sxpe_classify_owner_repo("${_gh_owner}" "${_gh_repo}" SXPE_GIT_LINEAGE SXPE_GIT_SOURCE_URL)
    endif()
endif()

if(DEFINED ENV{GITHUB_HEAD_REF} AND NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
    set(SXPE_GIT_BRANCH "$ENV{GITHUB_HEAD_REF}")
elseif(SXPE_GIT_BRANCH STREQUAL "" AND DEFINED ENV{GITHUB_REF_NAME}
       AND NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
    set(SXPE_GIT_BRANCH "$ENV{GITHUB_REF_NAME}")
endif()
if(SXPE_GIT_BRANCH STREQUAL "")
    if(_in_git)
        set(SXPE_GIT_BRANCH "detached")
    else()
        set(SXPE_GIT_BRANCH "unknown")
    endif()
endif()

if((SXPE_GIT_COMMIT STREQUAL "" OR SXPE_GIT_COMMIT STREQUAL "unknown")
   AND DEFINED ENV{GITHUB_SHA} AND NOT "$ENV{GITHUB_SHA}" STREQUAL "")
    set(SXPE_GIT_COMMIT "$ENV{GITHUB_SHA}")
endif()
if(SXPE_GIT_COMMIT STREQUAL "")
    set(SXPE_GIT_COMMIT "unknown")
endif()

function(sxpe_c_escape in out)
    string(REPLACE "\\" "\\\\" t "${in}")
    string(REPLACE "\"" "\\\"" t "${t}")
    string(REPLACE "\n" " " t "${t}")
    set(${out} "${t}" PARENT_SCOPE)
endfunction()

sxpe_c_escape("${SXPE_GIT_BRANCH}" SXPE_GIT_BRANCH)
sxpe_c_escape("${SXPE_GIT_COMMIT}" SXPE_GIT_COMMIT)
sxpe_c_escape("${SXPE_BUILD_UTC}" SXPE_BUILD_UTC)
sxpe_c_escape("${SXPE_GIT_LINEAGE}" SXPE_GIT_LINEAGE)
sxpe_c_escape("${SXPE_GIT_SOURCE_URL}" SXPE_GIT_SOURCE_URL)
sxpe_c_escape("${SXPE_GIT_CANONICAL_URL}" SXPE_GIT_CANONICAL_URL)

get_filename_component(_out_dir "${SXPE_BUILD_INFO_OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")
configure_file("${SXPE_BUILD_INFO_IN}" "${SXPE_BUILD_INFO_OUT}" @ONLY)
