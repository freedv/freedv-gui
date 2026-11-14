add_library(fdv_logging STATIC
    AdifRecord.cpp
    WSJTXNetworkLogger.cpp
)

target_compile_options(fdv_logging PRIVATE ${WARNINGS_AS_ERRORS_FLAGS})
target_link_libraries(fdv_logging PUBLIC git_version fdv_util)

if(UNITTEST)
DefineFreeDVUnitTest(logging WSJTXPacketBuilderTest fdv_logging)
DefineFreeDVUnitTest(logging WSJTXNetworkLoggerTest fdv_logging)
DefineFreeDVUnitTest(logging AdifRecordTest fdv_logging)
target_include_directories(WSJTXNetworkLoggerTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif(UNITTEST)
