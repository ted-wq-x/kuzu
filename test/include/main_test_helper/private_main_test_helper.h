#pragma once

#include "graph_test/graph_test.h"

namespace kuzu {
namespace testing {

class PrivateApiTest : public DBTest {
public:
    void SetUp() override {
        BaseGraphTest::SetUp();
        createDBAndConn();
        initGraph();
    }

    std::string getInputDir() override {
        return TestHelper::appendKuzuRootPath("dataset/tinysnb/");
    }

    static void assertMatchPersonCountStar(main::Connection* conn) {
        auto result = conn->query("MATCH (a:person) RETURN COUNT(*)");
        ASSERT_TRUE(result->hasNext());
        auto tuple = result->getNext();
        ASSERT_EQ(tuple->getValue(0)->getValue<int64_t>(), 8);
        ASSERT_FALSE(result->hasNext());
    }
};

} // namespace testing
} // namespace kuzu
