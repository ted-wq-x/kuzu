#pragma once

#include "function/function.h"

namespace kuzu {
namespace function {

struct DatePartFunction {
    static constexpr const char* name = "DATE_PART";

    static function_set getFunctionSet();
};

struct DatePartFunctionAlias {
    using alias = DatePartFunction;

    static constexpr const char* name = "DATEPART";
};

struct DateTruncFunction {
    static constexpr const char* name = "DATE_TRUNC";

    static function_set getFunctionSet();
};

struct DateTruncFunctionAlias {
    using alias = DateTruncFunction;

    static constexpr const char* name = "DATETRUNC";
};

struct DayNameFunction {
    static constexpr const char* name = "DAYNAME";

    static function_set getFunctionSet();
};

struct GreatestFunction {
    static constexpr const char* name = "GREATEST";

    static function_set getFunctionSet();
};

struct LastDayFunction {
    static constexpr const char* name = "LAST_DAY";

    static function_set getFunctionSet();
};

struct LeastFunction {
    static constexpr const char* name = "LEAST";

    static function_set getFunctionSet();
};

struct MakeDateFunction {
    static constexpr const char* name = "MAKE_DATE";

    static function_set getFunctionSet();
};

struct MonthNameFunction {
    static constexpr const char* name = "MONTHNAME";

    static function_set getFunctionSet();
};

struct DateYearFunction {
    static constexpr const char* name = "YEAR";

    static function_set getFunctionSet();
};

struct DateMonthFunction {
    static constexpr const char* name = "MONTH";

    static function_set getFunctionSet();
};

struct DateDayFunction {
    static constexpr const char* name = "DAY";

    static function_set getFunctionSet();
};

struct DateHourFunction {
    static constexpr const char* name = "HOUR";

    static function_set getFunctionSet();
};

struct DateMinuteFunction {
    static constexpr const char* name = "MINUTE";

    static function_set getFunctionSet();
};

struct DateSecondFunction {
    static constexpr const char* name = "SECOND";

    static function_set getFunctionSet();
};

struct AddDayFunction {
    static constexpr const char* name = "ADD_DAY";

    static function_set getFunctionSet();
};

struct AddHourFunction {
    static constexpr const char* name = "ADD_HOUR";

    static function_set getFunctionSet();
};

struct AddMinuteFunction {
    static constexpr const char* name = "ADD_MINUTE";

    static function_set getFunctionSet();
};

struct AddSecondFunction {
    static constexpr const char* name = "ADD_SECOND";

    static function_set getFunctionSet();
};

struct CurrentDateFunction {
    static constexpr const char* name = "CURRENT_DATE";

    static function_set getFunctionSet();
};

struct CurrentTimestampFunction {
    static constexpr const char* name = "CURRENT_TIMESTAMP";

    static function_set getFunctionSet();
};

} // namespace function
} // namespace kuzu
