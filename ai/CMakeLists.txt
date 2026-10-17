# Shared AI core used by the desktop shell and the mobile/Kirigami app.
#
# The UI lives in shell/ai (Qt Widgets) and mobile/components/ai (Qt Quick).
# This target contains only portable Qt/KF6 code: configuration, networking,
# streaming, model discovery and web search.

add_library(okular_ai_core STATIC
    aiconfig.cpp
    aiconfig.h
    embeddingclient.cpp
    embeddingclient.h
    llmclient.cpp
    llmclient.h
    modelcatalog.cpp
    modelcatalog.h
    websearchclient.cpp
    websearchclient.h
)

set_target_properties(okular_ai_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

target_include_directories(okular_ai_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(okular_ai_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        KF6::ConfigCore
)
