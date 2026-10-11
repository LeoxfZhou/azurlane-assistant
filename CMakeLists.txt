cmake_minimum_required(VERSION 3.20)
project(AzurLane LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_compile_options(-Wall -Wextra -Wpedantic)
set(GAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(game_services STATIC src/game_services.cpp)
target_include_directories(game_services PUBLIC "${GAME_ROOT}/src")
add_library(commander_cat_core STATIC
    src/commander_cat_assessment.cpp
    src/commander_cat_database.cpp
    src/commander_cat_embryo.cpp
    src/commander_cat_fleets.cpp
    src/commander_cat_inventory.cpp
    src/commander_cat_model.cpp
    src/commander_cat_settings.cpp
)
target_include_directories(commander_cat_core PUBLIC "${GAME_ROOT}/src")
add_executable(azur_lane_gui src/main.cpp)
target_link_libraries(azur_lane_gui PRIVATE game_services commander_cat_core)
target_compile_definitions(azur_lane_gui PRIVATE
    GAME_USER_DATA_DIR="${GAME_ROOT}/user_data"
    GAME_CACHE_DIR="${GAME_ROOT}/cache"
    GAME_GUI_WEB_DIR="${GAME_ROOT}/web"
    GAME_DATA_DIR="${GAME_ROOT}/data"
    GAME_DATA_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_game_data.py"
    AZUR_LANE_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    GAME_GUI_DEFAULT_ROUTE="azur-lane"
    AZUR_LANE_COMMANDER_CAT_DATA_DIR="${GAME_ROOT}/data/commander_cats"
    GAME_SHIP_STORY_DATA_DIR="${GAME_ROOT}/data/ship_stories"
    GAME_SHIP_PROGRESS_SEED_FILE="${GAME_ROOT}/data/migration/legacy_ship_inventory.csv"
    AZUR_LANE_WIKI_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_commander_cat_wiki_assets.py"
    AZUR_LANE_WIKI_REFRESH_PREVIEW="${GAME_ROOT}/cache/commander_cat_wiki_refresh_preview.json"
    COMMANDER_CAT_IMPORT_SCRIPT="${GAME_ROOT}/tools/prepare_commander_cat_import.py"
)
include(CTest)
if(BUILD_TESTING)
add_test(NAME standalone_http_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/standalone_http_tests.py" $<TARGET_FILE:azur_lane_gui>)
add_executable(game_services_tests tests/game_services_tests.cpp)
target_link_libraries(game_services_tests PRIVATE game_services)
target_compile_definitions(game_services_tests PRIVATE
    GAME_USER_DATA_DIR="${GAME_ROOT}/user_data"
    GAME_CACHE_DIR="${GAME_ROOT}/cache"
    GAME_GUI_WEB_DIR="${GAME_ROOT}/web"
    GAME_DATA_DIR="${GAME_ROOT}/data"
    GAME_DATA_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_game_data.py"
    AZUR_LANE_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    GAME_GUI_DEFAULT_ROUTE="azur-lane"
    AZUR_LANE_COMMANDER_CAT_DATA_DIR="${GAME_ROOT}/data/commander_cats"
    GAME_SHIP_STORY_DATA_DIR="${GAME_ROOT}/data/ship_stories"
    GAME_SHIP_PROGRESS_SEED_FILE="${GAME_ROOT}/data/migration/legacy_ship_inventory.csv"
    AZUR_LANE_WIKI_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_commander_cat_wiki_assets.py"
    AZUR_LANE_WIKI_REFRESH_PREVIEW="${GAME_ROOT}/cache/commander_cat_wiki_refresh_preview.json"
    COMMANDER_CAT_IMPORT_SCRIPT="${GAME_ROOT}/tools/prepare_commander_cat_import.py"
)
add_test(NAME game_services_tests COMMAND game_services_tests)
add_executable(commander_cat_tests tests/commander_cat_tests.cpp)
target_link_libraries(commander_cat_tests PRIVATE commander_cat_core)
target_compile_definitions(commander_cat_tests PRIVATE
    GAME_USER_DATA_DIR="${GAME_ROOT}/user_data"
    GAME_CACHE_DIR="${GAME_ROOT}/cache"
    GAME_GUI_WEB_DIR="${GAME_ROOT}/web"
    GAME_DATA_DIR="${GAME_ROOT}/data"
    GAME_DATA_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_game_data.py"
    AZUR_LANE_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    GAME_GUI_DEFAULT_ROUTE="azur-lane"
    AZUR_LANE_COMMANDER_CAT_DATA_DIR="${GAME_ROOT}/data/commander_cats"
    GAME_SHIP_STORY_DATA_DIR="${GAME_ROOT}/data/ship_stories"
    GAME_SHIP_PROGRESS_SEED_FILE="${GAME_ROOT}/data/migration/legacy_ship_inventory.csv"
    AZUR_LANE_WIKI_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_commander_cat_wiki_assets.py"
    AZUR_LANE_WIKI_REFRESH_PREVIEW="${GAME_ROOT}/cache/commander_cat_wiki_refresh_preview.json"
    COMMANDER_CAT_IMPORT_SCRIPT="${GAME_ROOT}/tools/prepare_commander_cat_import.py"
)
add_test(NAME commander_cat_tests COMMAND commander_cat_tests)
add_executable(commander_cat_embryo_tests tests/commander_cat_embryo_tests.cpp)
target_link_libraries(commander_cat_embryo_tests PRIVATE commander_cat_core)
target_compile_definitions(commander_cat_embryo_tests PRIVATE
    GAME_USER_DATA_DIR="${GAME_ROOT}/user_data"
    GAME_CACHE_DIR="${GAME_ROOT}/cache"
    GAME_GUI_WEB_DIR="${GAME_ROOT}/web"
    GAME_DATA_DIR="${GAME_ROOT}/data"
    GAME_DATA_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_game_data.py"
    AZUR_LANE_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    GAME_GUI_DEFAULT_ROUTE="azur-lane"
    AZUR_LANE_COMMANDER_CAT_DATA_DIR="${GAME_ROOT}/data/commander_cats"
    GAME_SHIP_STORY_DATA_DIR="${GAME_ROOT}/data/ship_stories"
    GAME_SHIP_PROGRESS_SEED_FILE="${GAME_ROOT}/data/migration/legacy_ship_inventory.csv"
    AZUR_LANE_WIKI_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_commander_cat_wiki_assets.py"
    AZUR_LANE_WIKI_REFRESH_PREVIEW="${GAME_ROOT}/cache/commander_cat_wiki_refresh_preview.json"
    COMMANDER_CAT_IMPORT_SCRIPT="${GAME_ROOT}/tools/prepare_commander_cat_import.py"
)
add_test(NAME commander_cat_embryo_tests COMMAND commander_cat_embryo_tests)
add_executable(commander_cat_fleets_tests tests/commander_cat_fleets_tests.cpp)
target_link_libraries(commander_cat_fleets_tests PRIVATE commander_cat_core)
target_compile_definitions(commander_cat_fleets_tests PRIVATE
    GAME_USER_DATA_DIR="${GAME_ROOT}/user_data"
    GAME_CACHE_DIR="${GAME_ROOT}/cache"
    GAME_GUI_WEB_DIR="${GAME_ROOT}/web"
    GAME_DATA_DIR="${GAME_ROOT}/data"
    GAME_DATA_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_game_data.py"
    AZUR_LANE_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    GAME_GUI_DEFAULT_ROUTE="azur-lane"
    AZUR_LANE_COMMANDER_CAT_DATA_DIR="${GAME_ROOT}/data/commander_cats"
    GAME_SHIP_STORY_DATA_DIR="${GAME_ROOT}/data/ship_stories"
    GAME_SHIP_PROGRESS_SEED_FILE="${GAME_ROOT}/data/migration/legacy_ship_inventory.csv"
    AZUR_LANE_WIKI_REFRESH_SCRIPT="${GAME_ROOT}/tools/refresh_commander_cat_wiki_assets.py"
    AZUR_LANE_WIKI_REFRESH_PREVIEW="${GAME_ROOT}/cache/commander_cat_wiki_refresh_preview.json"
    COMMANDER_CAT_IMPORT_SCRIPT="${GAME_ROOT}/tools/prepare_commander_cat_import.py"
)
add_test(NAME commander_cat_fleets_tests COMMAND commander_cat_fleets_tests)
add_test(NAME cat_offline_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/cat_offline_tests.py")
add_test(NAME cat_transfer_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/cat_transfer_tests.py")
add_test(NAME commander_cat_golden_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/commander_cat_golden_tests.py" $<TARGET_FILE:commander_cat_embryo_tests>)
add_test(NAME commander_cat_import_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/commander_cat_import_tests.py")
add_test(NAME commander_cat_v2_http_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/commander_cat_v2_http_tests.py" $<TARGET_FILE:azur_lane_gui>)
add_test(NAME equipment_http_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/equipment_http_tests.py" $<TARGET_FILE:azur_lane_gui>)
add_test(NAME equipment_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/equipment_tests.py")
add_test(NAME fleet_plans_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/fleet_plans_tests.py")
add_test(NAME refresh_game_data_tests COMMAND "${Python3_EXECUTABLE}" "${GAME_ROOT}/tests/refresh_game_data_tests.py")
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
add_test(NAME fleet_ui_tests COMMAND "${NODE_EXECUTABLE}" "${GAME_ROOT}/tests/fleet_ui_tests.js")
add_test(NAME training_plan_tests COMMAND "${NODE_EXECUTABLE}" "${GAME_ROOT}/tests/training_plan_tests.js")
add_test(NAME app_syntax COMMAND "${NODE_EXECUTABLE}" --check "${GAME_ROOT}/web/app.js")
add_test(NAME cat_offline_syntax COMMAND "${NODE_EXECUTABLE}" --check "${GAME_ROOT}/web/cat_offline.js")
add_test(NAME equipment_syntax COMMAND "${NODE_EXECUTABLE}" --check "${GAME_ROOT}/web/equipment.js")
add_test(NAME fleet_plans_syntax COMMAND "${NODE_EXECUTABLE}" --check "${GAME_ROOT}/web/fleet_plans.js")
add_test(NAME training_plan_syntax COMMAND "${NODE_EXECUTABLE}" --check "${GAME_ROOT}/web/training_plan.js")
endif()
endif()
