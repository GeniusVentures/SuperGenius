# tier1_audit_check.cmake - CMake script-mode (-P) static audit gating the
# cold-storage Tier-1 contract (TEST-02 SC4, v1.1 Phase 12 plan 12-03
# Task 2 - the thirdparty/sgns-blockstore tier1_audit_check.cmake
# precedent, rescoped to SuperGenius's SC4 concerns). Fails with
# file:line diagnostics on any of THREE checks over
# test/src/crdt/CMakeLists.txt:
#
#  1. LABEL PARITY (the cold-storage region): every addtest(cold_
#     registration has a matching set_tests_properties LABELS line -
#     ctest -L never selects unlabeled tests, so an unlabeled suite
#     would silently vanish from the fail-closed Tier-1 lane. The
#     parity scope is the cold_ prefix (the Phase-11 proven command
#     shape): the crdt directory's LEGACY suites predate the tier
#     system and are deliberately unlabeled (they run in the default
#     ctest pass, not -L Tier1) - rescoping parity to every addtest
#     would fail on that pre-existing, consciously-accepted posture.
#
#  2. GATING PARITY: every addtest whose registration block links
#     sgns-blockstore::sgns-blockstore sits inside if(SGNS_COLD_STORAGE)
#     - a cold suite registered ungated would fail to CONFIGURE on OFF
#     builds (the fakes' relative include / cache var only exists for
#     consumers that opted in via the gate). The config-gate and
#     link-proof exemptions link no library target and must stay
#     outside the gate.
#
#  3. WALL-CLOCK SOAK CONTAINMENT: the cold_storage_wallclock_soak
#     registration carries NO Tier1 label (D-07: the 20-40 minute
#     dev-gated soak must never join the fail-closed lane).
#
# DELIBERATELY NOT PORTED from the library audit: the fixed-sleep
# pattern scan. SuperGenius cold suites use the sanctioned PollUntil
# deadline-polling helper (12-PATTERNS.md shared-patterns note; the
# 12-01 acceptance criterion already confines sleep_for to the PollUntil
# body per-file) - a whole-tree sleep scan would false-positive on the
# pacing sleeps of the proven actor-thread shapes (the Phase-10
# shutdown_under_load publisher precedent). This omission is a recorded
# scoping decision, not an oversight.
#
# Registered from test/CMakeLists.txt as:
#   add_test(NAME tier1_static_audit
#            COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -P
#                    ${CMAKE_CURRENT_SOURCE_DIR}/tier1_audit_check.cmake)
# with LABELS "Tier1" - belt-and-suspenders so the audit runs inside
# the same fail-closed lane it polices.

if(NOT DEFINED SRC_DIR)
  message(FATAL_ERROR "tier1_audit_check: SRC_DIR must be defined (-DSRC_DIR=<repo root>)")
endif()

set(_crdt_cmake "${SRC_DIR}/test/src/crdt/CMakeLists.txt")

if(NOT EXISTS "${_crdt_cmake}")
  message(FATAL_ERROR "tier1 audit violation: ${_crdt_cmake} not found")
endif()

file(READ "${_crdt_cmake}" _content)

# --- Check 1: cold-suite label parity (cold_ prefix scope) -------------
string(REGEX MATCHALL "addtest[(]cold_[a-z0-9_]+" _cold_registrations "${_content}")
string(REGEX MATCHALL "set_tests_properties[(]cold_[a-z0-9_]+" _cold_properties "${_content}")

list(LENGTH _cold_registrations _reg_count)
list(LENGTH _cold_properties _prop_count)

if(NOT _reg_count EQUAL _prop_count)
  message(FATAL_ERROR
    "tier1 audit violation: cold-suite label parity broken - "
    "${_reg_count} addtest(cold_*) registrations vs ${_prop_count} "
    "set_tests_properties(cold_*) label lines in test/src/crdt/CMakeLists.txt "
    "(every cold suite must carry an explicit label; ctest -L never selects "
    "unlabeled tests)")
endif()
if(_reg_count EQUAL 0)
  message(FATAL_ERROR
    "tier1 audit violation: no cold_ registrations found - the audit's "
    "glob scope is stale (cold suites exist in the tree)")
endif()

# --- Check 2: gated linking parity -------------------------------------
# Every target linking the library must sit inside if(SGNS_COLD_STORAGE).
# Strategy: walk the file line-by-line tracking gate depth; any line
# that links sgns-blockstore::sgns-blockstore outside the gate trips.
file(STRINGS "${_crdt_cmake}" _lines)
list(LENGTH _lines _line_total)
set(_gate_depth 0)
set(_line_no 0)
set(_violations "")
foreach(_line IN LISTS _lines)
  math(EXPR _line_no "${_line_no} + 1")
  if(_line MATCHES "^if[(]SGNS_COLD_STORAGE[)]")
    math(EXPR _gate_depth "${_gate_depth} + 1")
  elseif(_line MATCHES "^endif[(][)]")
    if(_gate_depth GREATER 0)
      math(EXPR _gate_depth "${_gate_depth} - 1")
    endif()
  endif()
  # Comments (header/registration prose mentioning the library name)
  # never link anything - skip them.
  if(_line MATCHES "sgns-blockstore::sgns-blockstore"
     AND _gate_depth EQUAL 0
     AND NOT _line MATCHES "^[ \t]*#")
    list(APPEND _violations
         "  test/src/crdt/CMakeLists.txt:${_line_no}: links sgns-blockstore::sgns-blockstore OUTSIDE if(SGNS_COLD_STORAGE): ${_line}")
  endif()
endforeach()

if(_violations)
  message(FATAL_ERROR
    "tier1 audit violation: ungated cold-suite link(s) - a suite linking "
    "the library must sit inside if(SGNS_COLD_STORAGE) or OFF builds fail "
    "at configure:\n${_violations}")
endif()

# --- Check 3: wall-clock soak containment ------------------------------
# The registration line itself must not carry a Tier1 label (the label
# line is separate, but belt-and-suspenders scans both).
string(REGEX MATCH "addtest[(]cold_storage_wallclock_soak[^\r\n]*Tier1[^\r\n]*" _bad_soaK "${_content}")
if(_bad_soaK)
  message(FATAL_ERROR
    "tier1 audit violation: cold_storage_wallclock_soak registration "
    "carries a Tier1 label - the dev-gated wall-clock soak must NEVER "
    "join the fail-closed lane (D-07)")
endif()
string(REGEX MATCH "set_tests_properties[(]cold_storage_wallclock_soak[^\r\n]*Tier1[^\r\n]*" _bad_soak "${_content}")
if(_bad_soak)
  message(FATAL_ERROR
    "tier1 audit violation: cold_storage_wallclock_soak carries a Tier1 "
    "label line - the dev-gated wall-clock soak must NEVER join the "
    "fail-closed lane (D-07)")
endif()

message(STATUS "tier1_static_audit: PASS (${_reg_count} cold suites labeled, "
               "library links gated, wall-clock soak contained)")
