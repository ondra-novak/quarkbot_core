target_sources(quarkbot_impl PRIVATE 
    backtest_executor.cpp 
    backtest_executor.hpp 
    backtest.cpp 
    backtest.hpp 
    simaccount.cpp 
    simaccount.hpp 
    simexec_report_csv.hpp 
    simexecutor.cpp 
    simexecutor.hpp 
    simexchange.cpp 
    simexchange.hpp 
    siminstrument.cpp 
    siminstrument.hpp 
    simtradableinstrument.cpp
    simtradableinstrument.hpp
    minute_data_source.cpp
    minute_data_source.hpp
    merged_data_source.cpp
    merged_data_source.hpp
    symbology_mapping.hpp
    datasource_config.cpp
    replay_csv_file.hpp
    replay_csv_file.cpp
    instrument_config.hpp
    instrument_config.cpp
)
target_include_directories(quarkbot_impl PRIVATE ${CMAKE_BINARY_DIR}/)