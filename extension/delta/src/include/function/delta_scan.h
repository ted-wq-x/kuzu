#pragma once

#include "binder/binder.h"
#include "connector/connector_factory.h"
#include "connector/delta_connector.h"
#include "connector/duckdb_result_converter.h"
#include "connector/duckdb_type_converter.h"
#include "function/table/table_function.h"

namespace kuzu {
namespace delta_extension {

struct DeltaScanFunction {
    static constexpr const char* name = "DELTA_SCAN";

    static function::function_set getFunctionSet();
};

struct DeltaScanBindData final : function::ScanFileBindData {
    std::string query;
    std::shared_ptr<duckdb_extension::DuckDBConnector> connector;
    duckdb_extension::DuckDBResultConverter converter;

    DeltaScanBindData(std::string query,
        std::shared_ptr<duckdb_extension::DuckDBConnector> connector,
        duckdb_extension::DuckDBResultConverter converter, binder::expression_vector columns,
        main::ClientContext* context)
        : function::ScanFileBindData{std::move(columns), 0 /* numRows */, common::FileScanInfo{},
              context},
          query{std::move(query)}, connector{std::move(connector)},
          converter{std::move(converter)} {}

    std::unique_ptr<TableFuncBindData> copy() const override {
        return std::make_unique<DeltaScanBindData>(*this);
    }
};

// Functions and structs exposed for use
std::unique_ptr<function::TableFuncSharedState> initDeltaScanSharedState(
    const function::TableFuncInitSharedStateInput& input);

common::offset_t tableFunc(const function::TableFuncInput& input,
    function::TableFuncOutput& output);

} // namespace delta_extension
} // namespace kuzu
