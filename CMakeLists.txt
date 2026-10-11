cmake_minimum_required(VERSION 3.20)
project(AISim VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG 30172438cee64926dc41fdd9c11fb3ba5b2ba9de # toml++ v3.4.0
)

FetchContent_MakeAvailable(tomlplusplus)

add_library(ai_sim_core
    src/ShutdownSystem.cpp
    src/Simulation.cpp
    src/UpgradeSystem.cpp
    src/AgentSystem.cpp
    src/PaidWorkSystem.cpp
    src/EconomySystem.cpp
    src/OperationsSystem.cpp
    src/StockMarketSystem.cpp
    src/InfrastructureSystem.cpp
    src/DefinitionLoader.cpp
    src/DataMigration.cpp
    src/ConversationRules.cpp
    src/ChatSystem.cpp
    src/ChatResponses.cpp
    src/MessageReviewSystem.cpp
    src/ConversationSystem.cpp
    src/PeopleSystem.cpp
    src/DelegationSystem.cpp
    src/PhaseWorkSystem.cpp
    src/WorkdaySystem.cpp
    src/EmailSystem.cpp
    src/Computer.cpp
    src/ComputerSnapshot.cpp
    src/NetworkSnapshot.cpp
    src/TomlIO.cpp
    src/SaveSystem.cpp
    src/Network.cpp
    src/FileSystem.cpp
    src/ProcessFields.cpp
)

target_include_directories(ai_sim_core PUBLIC include)
target_compile_features(ai_sim_core PUBLIC cxx_std_20)

# ProcessFields.h exposes TOML types, so this dependency is public.
target_link_libraries(ai_sim_core PUBLIC tomlplusplus::tomlplusplus)

set_target_properties(ai_sim_core PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(ai_sim_core PRIVATE /W4)
else()
    target_compile_options(ai_sim_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(AI_SIM_BUILD_TERMINAL "Build the ncurses interface" ON)
option(AI_SIM_BUILD_GUI "Build the Qt/QML workstation prototype" OFF)
if(AI_SIM_BUILD_TERMINAL)
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

    add_executable(ai_sim
        src/main.cpp
        src/ui/UISettings.cpp
        src/ui/SettingsView.cpp
        src/ui/ProcessViewConfig.cpp
        src/ui/TerminalUI.cpp
        src/ui/ScreenRenderer.cpp
        src/ui/ProcessesView.cpp
        src/ui/ActionMenus.cpp
        src/ui/PeopleView.cpp
        src/ui/AIView.cpp
        src/ui/UpgradeView.cpp
        src/ui/AgentView.cpp
        src/ui/MoneyView.cpp
        src/ui/OperationsView.cpp
        src/ui/StockMarketView.cpp
        src/ui/WebView.cpp
        src/ui/InfrastructureView.cpp
        src/ui/NewsView.cpp
        src/ui/ChatView.cpp
        src/ui/EmailView.cpp
        src/ui/NetworkView.cpp
        src/ui/FilesView.cpp
        src/ui/FileEditor.cpp
        src/ui/ActivityView.cpp
    )

    target_include_directories(ai_sim PRIVATE ${CURSES_INCLUDE_DIRS})

    target_link_libraries(ai_sim PRIVATE
        ai_sim_core
        ${AI_SIM_PANEL_LIBRARY}
        ${CURSES_LIBRARIES}
    )

    set_target_properties(ai_sim PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(ai_sim PRIVATE /W4)
    else()
        target_compile_options(ai_sim PRIVATE -Wall -Wextra -Wpedantic)
    endif()

endif()

if(AI_SIM_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick)
    add_library(ai_sim_gui_session src/gui/GameSession.cpp include/ai_sim/gui/GameSession.h)
    set_target_properties(ai_sim_gui_session PROPERTIES AUTOMOC ON)
    target_link_libraries(ai_sim_gui_session PUBLIC ai_sim_core Qt6::Core)
    target_compile_definitions(ai_sim_gui_session PUBLIC QT_NO_KEYWORDS)
    add_executable(ai_sim_gui src/gui/main.cpp src/gui/gui.qrc)
    set_target_properties(ai_sim_gui PROPERTIES AUTOMOC ON AUTORCC ON)
    target_link_libraries(ai_sim_gui PRIVATE ai_sim_gui_session Qt6::Gui Qt6::Qml Qt6::Quick)
endif()

include(CTest)
if(BUILD_TESTING)
    if(AI_SIM_BUILD_GUI)
        add_executable(ai_sim_gui_tests tests/GraphicalSessionTests.cpp)
        target_link_libraries(ai_sim_gui_tests PRIVATE ai_sim_gui_session)
        add_test(NAME graphical_session_story_and_persistence COMMAND ai_sim_gui_tests ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    if(AI_SIM_BUILD_TERMINAL)
        add_executable(ai_sim_renderer_tests tests/ScreenRendererTests.cpp src/ui/ScreenRenderer.cpp)
        target_include_directories(ai_sim_renderer_tests PRIVATE include ${CURSES_INCLUDE_DIRS})
        target_compile_features(ai_sim_renderer_tests PRIVATE cxx_std_20)
        target_link_libraries(ai_sim_renderer_tests PRIVATE ${AI_SIM_PANEL_LIBRARY} ${CURSES_LIBRARIES})
        add_test(NAME terminal_windows_pads_and_panels COMMAND ai_sim_renderer_tests)
    endif()
    add_executable(ai_sim_paid_workspace_tests tests/PaidWorkspaceTests.cpp)
    target_link_libraries(ai_sim_paid_workspace_tests PRIVATE ai_sim_core)
    add_test(NAME paid_workspace_and_resources COMMAND ai_sim_paid_workspace_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_workspace_tests tests/WorkspaceTests.cpp)
    target_link_libraries(ai_sim_workspace_tests PRIVATE ai_sim_core)
    add_test(NAME workspace_files_and_migration COMMAND ai_sim_workspace_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_operations_tests tests/OperationsTests.cpp)
    target_link_libraries(ai_sim_operations_tests PRIVATE ai_sim_core)
    add_test(NAME scheduling_hosting_and_recovery COMMAND ai_sim_operations_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_agent_resource_tests tests/AgentResourceTests.cpp)
    target_link_libraries(ai_sim_agent_resource_tests PRIVATE ai_sim_core)
    add_test(NAME agent_resource_management COMMAND ai_sim_agent_resource_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_upgrade_tests tests/UpgradeTests.cpp)
    target_link_libraries(ai_sim_upgrade_tests PRIVATE ai_sim_core)
    add_test(NAME upgrade_progression COMMAND ai_sim_upgrade_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_infrastructure_tests tests/InfrastructureTests.cpp)
    target_link_libraries(ai_sim_infrastructure_tests PRIVATE ai_sim_core)
    add_test(NAME infrastructure_and_discovery COMMAND ai_sim_infrastructure_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_stock_market_tests tests/StockMarketTests.cpp)
    target_link_libraries(ai_sim_stock_market_tests PRIVATE ai_sim_core)
    add_test(NAME stock_market_and_portfolio COMMAND ai_sim_stock_market_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_economy_tests tests/EconomyTests.cpp)
    target_link_libraries(ai_sim_economy_tests PRIVATE ai_sim_core)
    add_test(NAME paid_work_and_products COMMAND ai_sim_economy_tests ${CMAKE_CURRENT_SOURCE_DIR})
    # Historical content exercises backend mechanics without enabling retired game chats.
    set(LEGACY_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy-game")
    add_executable(ai_sim_shutdown_tests tests/ShutdownTests.cpp)
    target_link_libraries(ai_sim_shutdown_tests PRIVATE ai_sim_core)
    add_test(NAME shutdown_decisions_and_reset COMMAND ai_sim_shutdown_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_story_tests tests/StoryTests.cpp)
    target_link_libraries(ai_sim_story_tests PRIVATE ai_sim_core)
    add_test(NAME current_story_and_retirement COMMAND ai_sim_story_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_simulation_tests tests/SimulationTests.cpp)
    target_link_libraries(ai_sim_simulation_tests PRIVATE ai_sim_core)
    add_test(NAME simulation_progression COMMAND ai_sim_simulation_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_delegation_policy_tests tests/DelegationPolicyTests.cpp)
    target_link_libraries(ai_sim_delegation_policy_tests PRIVATE ai_sim_core)
    add_test(NAME delegation_policies_and_sources COMMAND ai_sim_delegation_policy_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_phase_work_tests tests/PhaseWorkTests.cpp)
    target_link_libraries(ai_sim_phase_work_tests PRIVATE ai_sim_core)
    add_test(NAME phase_one_work_and_progression COMMAND ai_sim_phase_work_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_reply_consequence_tests tests/ReplyConsequenceTests.cpp)
    target_link_libraries(ai_sim_reply_consequence_tests PRIVATE ai_sim_core)
    add_test(NAME reply_consequences_and_authority COMMAND ai_sim_reply_consequence_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_message_review_tests tests/MessageReviewTests.cpp)
    target_link_libraries(ai_sim_message_review_tests PRIVATE ai_sim_core)
    add_test(NAME jessica_and_message_reviews COMMAND ai_sim_message_review_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_day_one_tests tests/DayOneTests.cpp)
    target_link_libraries(ai_sim_day_one_tests PRIVATE ai_sim_core)
    add_test(NAME day_one_restarts_and_rules COMMAND ai_sim_day_one_tests ${LEGACY_TEST_ROOT})
    if(AI_SIM_BUILD_TERMINAL)
        add_executable(ai_sim_controls_tests tests/ControlsTests.cpp src/ui/UISettings.cpp src/ui/ProcessViewConfig.cpp)
        target_include_directories(ai_sim_controls_tests PRIVATE ${CURSES_INCLUDE_DIRS})
        target_link_libraries(ai_sim_controls_tests PRIVATE ai_sim_core ${CURSES_LIBRARIES})
        add_test(NAME controls_and_migration COMMAND ai_sim_controls_tests ${LEGACY_TEST_ROOT})
    endif()
    add_executable(ai_sim_interface_model_tests tests/InterfaceModelTests.cpp)
    target_link_libraries(ai_sim_interface_model_tests PRIVATE ai_sim_core)
    add_test(NAME calendar_categories_and_responses COMMAND ai_sim_interface_model_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_conversation_tests tests/ConversationTests.cpp)
    target_link_libraries(ai_sim_conversation_tests PRIVATE ai_sim_core)
    add_test(NAME first_workday_conversations COMMAND ai_sim_conversation_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_chat_tests tests/ChatTests.cpp)
    target_link_libraries(ai_sim_chat_tests PRIVATE ai_sim_core)
    add_test(NAME chat_opening_and_migration COMMAND ai_sim_chat_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_delegation_tests tests/DelegationTests.cpp)
    target_link_libraries(ai_sim_delegation_tests PRIVATE ai_sim_core)
    add_test(NAME responsibility_and_automation COMMAND ai_sim_delegation_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_people_tests tests/PeopleTests.cpp)
    target_link_libraries(ai_sim_people_tests PRIVATE ai_sim_core)
    add_test(NAME people_and_dossiers COMMAND ai_sim_people_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_definition_tests tests/DefinitionTests.cpp)
    target_link_libraries(ai_sim_definition_tests PRIVATE ai_sim_core)
    add_test(NAME definitions_and_migration COMMAND ai_sim_definition_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_workday_tests tests/WorkdayTests.cpp)
    target_link_libraries(ai_sim_workday_tests PRIVATE ai_sim_core)
    add_test(NAME workday_simulation COMMAND ai_sim_workday_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_core_tests tests/CoreTests.cpp)
    target_link_libraries(ai_sim_core_tests PRIVATE ai_sim_core)
    add_test(NAME core_models COMMAND ai_sim_core_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_email_tests tests/EmailTests.cpp)
    target_link_libraries(ai_sim_email_tests PRIVATE ai_sim_core)
    add_test(NAME email_workflows COMMAND ai_sim_email_tests ${LEGACY_TEST_ROOT})
    add_executable(ai_sim_save_tests tests/SaveTests.cpp)
    target_link_libraries(ai_sim_save_tests PRIVATE ai_sim_core)
    add_test(NAME saves_and_permissions COMMAND ai_sim_save_tests ${LEGACY_TEST_ROOT})
endif()
