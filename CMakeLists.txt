# Build independent simulation and terminal-UI libraries; the launcher links only their public interfaces.
cmake_minimum_required(VERSION 3.20)
project(AISim VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
set(AI_SIM_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Development Tools/tests")

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG 30172438cee64926dc41fdd9c11fb3ba5b2ba9de # toml++ v3.4.0
)

FetchContent_MakeAvailable(tomlplusplus)

add_library(ai_sim_core
    Source/Systems/ShutdownSystem.cpp
    Source/Simulation.cpp
    Source/Systems/UpgradeSystem.cpp
    Source/Systems/AgentSystem.cpp
    Source/Systems/PaidWorkSystem.cpp
    Source/Systems/EconomySystem.cpp
    Source/Systems/OperationsSystem.cpp
    Source/StockMarketSystem.cpp
    Source/Systems/InfrastructureSystem.cpp
    Source/DefinitionLoader.cpp
    Source/DataMigration.cpp
    Source/ConversationRules.cpp
    Source/Systems/ChatSystem.cpp
    Source/ChatResponses.cpp
    Source/Systems/MessageReviewSystem.cpp
    Source/Systems/ConversationSystem.cpp
    Source/PeopleSystem.cpp
    Source/Systems/DelegationSystem.cpp
    Source/Systems/PhaseWorkSystem.cpp
    Source/Systems/WorkdaySystem.cpp
    Source/EmailSystem.cpp
    Source/Computer.cpp
    Source/ComputerSnapshot.cpp
    Source/NetworkSnapshot.cpp
    Source/TomlIO.cpp
    Source/Systems/SaveSystem.cpp
    Source/Network.cpp
    Source/FileSystem.cpp
    Source/ProcessFields.cpp
)

target_include_directories(ai_sim_core PUBLIC data/include)
target_compile_features(ai_sim_core PUBLIC cxx_std_20)

# ProcessFields.h exposes TOML types, so this dependency is public.
target_link_libraries(ai_sim_core PUBLIC tomlplusplus::tomlplusplus)

set_target_properties(ai_sim_core PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(ai_sim_core PRIVATE /W4)
else()
    target_compile_options(ai_sim_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(CURSES_NEED_NCURSES TRUE)
find_package(Curses REQUIRED)
get_filename_component(AI_SIM_CURSES_LIBRARY_DIR "${CURSES_LIBRARY}" DIRECTORY)
if(CURSES_LIBRARIES MATCHES "ncursesw")
    set(AI_SIM_PANEL_NAME panelw)
else()
    set(AI_SIM_PANEL_NAME panel)
endif()
find_library(AI_SIM_PANEL_LIBRARY NAMES ${AI_SIM_PANEL_NAME}
    HINTS "${AI_SIM_CURSES_LIBRARY_DIR}" REQUIRED)

add_library(ai_sim_ui
    Source/ui/TextLayout.cpp
    Source/ui/UISettings.cpp
    Source/ui/SettingsView.cpp
    Source/ui/ProcessViewConfig.cpp
    Source/ui/TerminalUI.cpp
    Source/ui/RootUI.cpp
    Source/ui/PageInput.cpp
    Source/ui/ScreenRenderer.cpp
    Source/ui/ProcessesView.cpp
    Source/ui/ActionMenus.cpp
    Source/ui/PeopleView.cpp
    Source/ui/AIView.cpp
    Source/ui/UpgradeView.cpp
    Source/ui/AgentView.cpp
    Source/ui/MoneyView.cpp
    Source/ui/OperationsView.cpp
    Source/ui/StockMarketView.cpp
    Source/ui/WebView.cpp
    Source/ui/InfrastructureView.cpp
    Source/ui/NewsView.cpp
    Source/ui/ChatView.cpp
    Source/ui/EmailView.cpp
    Source/ui/NetworkView.cpp
    Source/ui/FilesView.cpp
    Source/ui/FileEditor.cpp
)

# RootUI, child views, controls and rendering share one library across game/tests.
target_include_directories(ai_sim_ui PUBLIC ${CURSES_INCLUDE_DIRS})
target_link_libraries(ai_sim_ui PUBLIC ai_sim_core ${AI_SIM_PANEL_LIBRARY} ${CURSES_LIBRARIES})
set_target_properties(ai_sim_ui PROPERTIES CXX_EXTENSIONS OFF)
add_executable(ai_sim Source/main.cpp)
target_link_libraries(ai_sim PRIVATE ai_sim_ui)
set_target_properties(ai_sim PROPERTIES CXX_EXTENSIONS OFF)
foreach(target IN ITEMS ai_sim_ui ai_sim)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    # Explicit fixture roots keep retired story content isolated from today's game.
    function(ai_sim_core_test target source test_name fixture_root)
        add_executable(${target} "${AI_SIM_TEST_ROOT}/${source}")
        target_link_libraries(${target} PRIVATE ai_sim_core)
        add_test(NAME ${test_name} COMMAND ${target} ${fixture_root})
    endfunction()
    add_executable(ai_sim_presentation_tests "${AI_SIM_TEST_ROOT}/PresentationConfigTests.cpp")
    target_link_libraries(ai_sim_presentation_tests PRIVATE ai_sim_ui)
    add_test(NAME toml_presentation_configuration COMMAND ai_sim_presentation_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_text_layout_tests "${AI_SIM_TEST_ROOT}/TextLayoutTests.cpp")
    target_link_libraries(ai_sim_text_layout_tests PRIVATE ai_sim_ui)
    add_test(NAME text_layout_narrow_panes_and_paragraphs COMMAND ai_sim_text_layout_tests)
    add_executable(ai_sim_root_ui_tests "${AI_SIM_TEST_ROOT}/RootUITests.cpp")
    target_include_directories(ai_sim_root_ui_tests PRIVATE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(ai_sim_root_ui_tests PRIVATE ai_sim_ui)
    add_test(NAME root_ui_children_focus_and_resize COMMAND ai_sim_root_ui_tests)
    add_executable(ai_sim_renderer_tests "${AI_SIM_TEST_ROOT}/ScreenRendererTests.cpp")
    target_include_directories(ai_sim_renderer_tests PRIVATE data/include ${CURSES_INCLUDE_DIRS})
    target_compile_features(ai_sim_renderer_tests PRIVATE cxx_std_20)
    target_link_libraries(ai_sim_renderer_tests PRIVATE ai_sim_ui)
    add_test(NAME terminal_windows_pads_and_panels COMMAND ai_sim_renderer_tests)
    ai_sim_core_test(ai_sim_paid_workspace_tests PaidWorkspaceTests.cpp paid_workspace_and_resources ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_workspace_tests WorkspaceTests.cpp workspace_files_and_migration ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_operations_tests OperationsTests.cpp scheduling_hosting_and_recovery ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_agent_resource_tests AgentResourceTests.cpp agent_resource_management ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_upgrade_tests UpgradeTests.cpp upgrade_progression ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_infrastructure_tests InfrastructureTests.cpp infrastructure_and_discovery ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_stock_market_tests StockMarketTests.cpp stock_market_and_portfolio ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_economy_tests EconomyTests.cpp paid_work_and_products ${CMAKE_CURRENT_SOURCE_DIR})
    # Historical content exercises backend mechanics without enabling retired game chats.
    set(LEGACY_TEST_ROOT "${AI_SIM_TEST_ROOT}/fixtures/legacy-game")
    ai_sim_core_test(ai_sim_shutdown_tests ShutdownTests.cpp shutdown_decisions_and_reset ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_story_tests StoryTests.cpp current_story_and_retirement ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_simulation_tests SimulationTests.cpp simulation_progression ${CMAKE_CURRENT_SOURCE_DIR})
    ai_sim_core_test(ai_sim_delegation_policy_tests DelegationPolicyTests.cpp delegation_policies_and_sources ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_phase_work_tests PhaseWorkTests.cpp phase_one_work_and_progression ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_reply_consequence_tests ReplyConsequenceTests.cpp reply_consequences_and_authority ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_message_review_tests MessageReviewTests.cpp jessica_and_message_reviews ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_day_one_tests DayOneTests.cpp day_one_restarts_and_rules ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_controls_tests "${AI_SIM_TEST_ROOT}/ControlsTests.cpp")
    target_include_directories(ai_sim_controls_tests PRIVATE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(ai_sim_controls_tests PRIVATE ai_sim_ui)
    add_test(NAME controls_and_migration COMMAND ai_sim_controls_tests ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_interface_model_tests InterfaceModelTests.cpp calendar_categories_and_responses ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_conversation_tests ConversationTests.cpp first_workday_conversations ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_chat_tests ChatTests.cpp chat_opening_and_migration ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_delegation_tests DelegationTests.cpp responsibility_and_automation ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_people_tests PeopleTests.cpp people_and_dossiers ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_definition_tests DefinitionTests.cpp definitions_and_migration ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_workday_tests WorkdayTests.cpp workday_simulation ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_core_tests CoreTests.cpp core_models ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_email_tests EmailTests.cpp email_workflows ${LEGACY_TEST_ROOT})
    ai_sim_core_test(ai_sim_save_tests SaveTests.cpp saves_and_permissions ${LEGACY_TEST_ROOT})
endif()
