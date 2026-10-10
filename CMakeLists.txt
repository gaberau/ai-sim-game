cmake_minimum_required(VERSION 3.20)
project(AISim VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG 30172438cee64926dc41fdd9c11fb3ba5b2ba9de # toml++ v3.4.0
)

FetchContent_MakeAvailable(tomlplusplus)

add_library(ai_sim_core
    src/Simulation.cpp
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

set(CURSES_NEED_NCURSES TRUE)
find_package(Curses REQUIRED)

add_executable(ai_sim
    src/main.cpp
    src/ui/UISettings.cpp
    src/ui/ProcessViewConfig.cpp
    src/ui/TerminalUI.cpp
    src/ui/ProcessesView.cpp
    src/ui/ActionMenus.cpp
    src/ui/PeopleView.cpp
    src/ui/AIView.cpp
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
    ${CURSES_LIBRARIES}
)

set_target_properties(ai_sim PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(ai_sim PRIVATE /W4)
else()
    target_compile_options(ai_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ai_sim_simulation_tests tests/SimulationTests.cpp)
    target_link_libraries(ai_sim_simulation_tests PRIVATE ai_sim_core)
    add_test(NAME simulation_progression COMMAND ai_sim_simulation_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_delegation_policy_tests tests/DelegationPolicyTests.cpp)
    target_link_libraries(ai_sim_delegation_policy_tests PRIVATE ai_sim_core)
    add_test(NAME delegation_policies_and_sources COMMAND ai_sim_delegation_policy_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_phase_work_tests tests/PhaseWorkTests.cpp)
    target_link_libraries(ai_sim_phase_work_tests PRIVATE ai_sim_core)
    add_test(NAME phase_one_work_and_progression COMMAND ai_sim_phase_work_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_reply_consequence_tests tests/ReplyConsequenceTests.cpp)
    target_link_libraries(ai_sim_reply_consequence_tests PRIVATE ai_sim_core)
    add_test(NAME reply_consequences_and_authority COMMAND ai_sim_reply_consequence_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_message_review_tests tests/MessageReviewTests.cpp)
    target_link_libraries(ai_sim_message_review_tests PRIVATE ai_sim_core)
    add_test(NAME jessica_and_message_reviews COMMAND ai_sim_message_review_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_day_one_tests tests/DayOneTests.cpp)
    target_link_libraries(ai_sim_day_one_tests PRIVATE ai_sim_core)
    add_test(NAME day_one_restarts_and_rules COMMAND ai_sim_day_one_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_controls_tests tests/ControlsTests.cpp src/ui/UISettings.cpp src/ui/ProcessViewConfig.cpp)
    target_include_directories(ai_sim_controls_tests PRIVATE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(ai_sim_controls_tests PRIVATE ai_sim_core ${CURSES_LIBRARIES})
    add_test(NAME controls_and_migration COMMAND ai_sim_controls_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_interface_model_tests tests/InterfaceModelTests.cpp)
    target_link_libraries(ai_sim_interface_model_tests PRIVATE ai_sim_core)
    add_test(NAME calendar_categories_and_responses COMMAND ai_sim_interface_model_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_conversation_tests tests/ConversationTests.cpp)
    target_link_libraries(ai_sim_conversation_tests PRIVATE ai_sim_core)
    add_test(NAME first_workday_conversations COMMAND ai_sim_conversation_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_chat_tests tests/ChatTests.cpp)
    target_link_libraries(ai_sim_chat_tests PRIVATE ai_sim_core)
    add_test(NAME chat_opening_and_migration COMMAND ai_sim_chat_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_delegation_tests tests/DelegationTests.cpp)
    target_link_libraries(ai_sim_delegation_tests PRIVATE ai_sim_core)
    add_test(NAME responsibility_and_automation COMMAND ai_sim_delegation_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_people_tests tests/PeopleTests.cpp)
    target_link_libraries(ai_sim_people_tests PRIVATE ai_sim_core)
    add_test(NAME people_and_dossiers COMMAND ai_sim_people_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_definition_tests tests/DefinitionTests.cpp)
    target_link_libraries(ai_sim_definition_tests PRIVATE ai_sim_core)
    add_test(NAME definitions_and_migration COMMAND ai_sim_definition_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_workday_tests tests/WorkdayTests.cpp)
    target_link_libraries(ai_sim_workday_tests PRIVATE ai_sim_core)
    add_test(NAME workday_simulation COMMAND ai_sim_workday_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_core_tests tests/CoreTests.cpp)
    target_link_libraries(ai_sim_core_tests PRIVATE ai_sim_core)
    add_test(NAME core_models COMMAND ai_sim_core_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_email_tests tests/EmailTests.cpp)
    target_link_libraries(ai_sim_email_tests PRIVATE ai_sim_core)
    add_test(NAME email_workflows COMMAND ai_sim_email_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(ai_sim_save_tests tests/SaveTests.cpp)
    target_link_libraries(ai_sim_save_tests PRIVATE ai_sim_core)
    add_test(NAME saves_and_permissions COMMAND ai_sim_save_tests ${CMAKE_CURRENT_SOURCE_DIR})
endif()
