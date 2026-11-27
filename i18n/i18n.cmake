# Non-invasive Chinese (zh_CN) i18n overlay integration for openhouse.
# Included via a single line in CMakeLists.txt to keep upstream merges conflict-free.

find_program(LH_I18N_PYTHON NAMES python3 python REQUIRED)

set(LH_I18N_DICT "${CMAKE_CURRENT_SOURCE_DIR}/i18n/zh_CN.json")
set(LH_I18N_BUILTIN_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/lh/i18n_builtin_zh_cn.h")

add_custom_command(
  OUTPUT "${LH_I18N_BUILTIN_HEADER}"
  COMMAND ${LH_I18N_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/i18n/gen_builtin.py"
          "${LH_I18N_DICT}" "${LH_I18N_BUILTIN_HEADER}"
  DEPENDS "${LH_I18N_DICT}" "${CMAKE_CURRENT_SOURCE_DIR}/i18n/gen_builtin.py"
  COMMENT "Generating built-in zh_CN translation table")

add_custom_target(lh_i18n_builtin DEPENDS "${LH_I18N_BUILTIN_HEADER}")

add_library(lh_i18n STATIC "${CMAKE_CURRENT_SOURCE_DIR}/i18n/i18n.cpp" "${LH_I18N_BUILTIN_HEADER}")
add_dependencies(lh_i18n lh_i18n_builtin)
set_target_properties(lh_i18n PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(lh_i18n PUBLIC cxx_std_20)
target_include_directories(lh_i18n PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i18n"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(lh_i18n PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(lh_i18n PUBLIC OPENHOUSE_I18N=1)

if(TARGET lh_imgui)
  target_sources(lh_imgui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/i18n/i18n_imgui.cpp")
  target_link_libraries(lh_imgui PUBLIC lh_i18n)
endif()

if(LH_BUILD_TESTS AND TARGET GTest::gtest_main)
  add_executable(lh_i18n_tests "${CMAKE_CURRENT_SOURCE_DIR}/i18n/test_i18n.cpp")
  target_link_libraries(lh_i18n_tests PRIVATE lh_i18n GTest::gtest_main)
  add_test(NAME i18n COMMAND lh_i18n_tests)
endif()
