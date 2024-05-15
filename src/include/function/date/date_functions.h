#pragma once

#include "common/assert.h"
#include "common/types/date_t.h"
#include "common/types/ku_string.h"
#include "common/types/timestamp_t.h"

namespace kuzu {
namespace function {

struct DayName {
    template<class T>
    static inline void operation(T& /*input*/, common::ku_string_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void DayName::operation(common::date_t& input, common::ku_string_t& result) {
    std::string dayName = common::Date::getDayName(input);
    result.set(dayName);
}

template<>
inline void DayName::operation(common::timestamp_t& input, common::ku_string_t& result) {
    common::dtime_t time{};
    common::date_t date{};
    common::Timestamp::convert(input, date, time);
    std::string dayName = common::Date::getDayName(date);
    result.set(dayName);
}

struct MonthName {
    template<class T>
    static inline void operation(T& /*input*/, common::ku_string_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void MonthName::operation(common::date_t& input, common::ku_string_t& result) {
    std::string monthName = common::Date::getMonthName(input);
    result.set(monthName);
}

template<>
inline void MonthName::operation(common::timestamp_t& input, common::ku_string_t& result) {
    common::dtime_t time{};
    common::date_t date{};
    common::Timestamp::convert(input, date, time);
    std::string monthName = common::Date::getMonthName(date);
    result.set(monthName);
}

struct LastDay {
    template<class T>

    static inline void operation(T& /*input*/, common::date_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void LastDay::operation(common::date_t& input, common::date_t& result) {
    result = common::Date::getLastDay(input);
}

template<>
inline void LastDay::operation(common::timestamp_t& input, common::date_t& result) {
    common::date_t date{};
    common::dtime_t time{};
    common::Timestamp::convert(input, date, time);
    result = common::Date::getLastDay(date);
}

struct DatePart {
    template<class LEFT_TYPE, class RIGHT_TYPE>
    static inline void operation(LEFT_TYPE& /*partSpecifier*/, RIGHT_TYPE& /*input*/,
        int64_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void DatePart::operation(common::ku_string_t& partSpecifier, common::date_t& input,
    int64_t& result) {
    common::DatePartSpecifier specifier;
    common::Interval::tryGetDatePartSpecifier(partSpecifier.getAsString(), specifier);
    result = common::Date::getDatePart(specifier, input);
}

template<>
inline void DatePart::operation(common::ku_string_t& partSpecifier, common::timestamp_t& input,
    int64_t& result) {
    common::DatePartSpecifier specifier;
    common::Interval::tryGetDatePartSpecifier(partSpecifier.getAsString(), specifier);
    result = common::Timestamp::getTimestampPart(specifier, input);
}

template<>
inline void DatePart::operation(common::ku_string_t& partSpecifier, common::interval_t& input,
    int64_t& result) {
    common::DatePartSpecifier specifier;
    common::Interval::tryGetDatePartSpecifier(partSpecifier.getAsString(), specifier);
    result = common::Interval::getIntervalPart(specifier, input);
}

struct DateTrunc {
    template<class LEFT_TYPE, class RIGHT_TYPE>
    static inline void operation(LEFT_TYPE& /*partSpecifier*/, RIGHT_TYPE& /*input*/,
        RIGHT_TYPE& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void DateTrunc::operation(common::ku_string_t& partSpecifier, common::date_t& input,
    common::date_t& result) {
    common::DatePartSpecifier specifier;
    common::Interval::tryGetDatePartSpecifier(partSpecifier.getAsString(), specifier);
    result = common::Date::trunc(specifier, input);
}

template<>
inline void DateTrunc::operation(common::ku_string_t& partSpecifier, common::timestamp_t& input,
    common::timestamp_t& result) {
    common::DatePartSpecifier specifier;
    common::Interval::tryGetDatePartSpecifier(partSpecifier.getAsString(), specifier);
    result = common::Timestamp::trunc(specifier, input);
}

struct Greatest {
    template<class T>
    static inline void operation(T& left, T& right, T& result) {
        result = left > right ? left : right;
    }
};

struct Least {
    template<class T>
    static inline void operation(T& left, T& right, T& result) {
        result = left > right ? right : left;
    }
};

struct MakeDate {
    static inline void operation(int64_t& year, int64_t& month, int64_t& day,
        common::date_t& result) {
        result = common::Date::fromDate(year, month, day);
    }
};

struct DateYearPart {
    template<class T>

    static inline void operation(T& /*input*/, int64_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void DateYearPart::operation(common::timestamp_t& input, int64_t& result) {
    result = common::Timestamp::getTimestampPart(common::DatePartSpecifier::YEAR, input);
}

template<>
inline void DateYearPart::operation(common::date_t& input, int64_t& result) {
    result = common::Date::getDatePart(common::DatePartSpecifier::YEAR, input);
}

struct DateMonthPart {
    template<class T>

    static inline void operation(T& /*input*/, int64_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void DateMonthPart::operation(common::timestamp_t& input, int64_t& result) {
    result = common::Timestamp::getTimestampPart(common::DatePartSpecifier::MONTH, input);
}

template<>
inline void DateMonthPart::operation(common::date_t& input, int64_t& result) {
    result = common::Date::getDatePart(common::DatePartSpecifier::MONTH, input);
}

struct DateDayPart {
    template<class T>

    static inline void operation(T& /*input*/, int64_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void DateDayPart::operation(common::timestamp_t& input, int64_t& result) {
    result = common::Timestamp::getTimestampPart(common::DatePartSpecifier::DAY, input);
}

template<>
inline void DateDayPart::operation(common::date_t& input, int64_t& result) {
    result = common::Date::getDatePart(common::DatePartSpecifier::DAY, input);
}

struct DateHourPart {
    template<class T>

    static inline void operation(T& /*input*/, int64_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void DateHourPart::operation(common::timestamp_t& input, int64_t& result) {
    result = common::Timestamp::getTimestampPart(common::DatePartSpecifier::HOUR, input);
}

template<>
inline void DateHourPart::operation(common::date_t& input, int64_t& result) {
    result = common::Date::getDatePart(common::DatePartSpecifier::HOUR, input);
}

struct DateMinutePart {
    template<class T>

    static inline void operation(T& /*input*/, int64_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void DateMinutePart::operation(common::timestamp_t& input, int64_t& result) {
    result = common::Timestamp::getTimestampPart(common::DatePartSpecifier::MINUTE, input);
}

template<>
inline void DateMinutePart::operation(common::date_t& input, int64_t& result) {
    result = common::Date::getDatePart(common::DatePartSpecifier::MINUTE, input);
}

struct DateSecondPart {
    template<class T>

    static inline void operation(T& /*input*/, int64_t& /*result*/) {
        KU_UNREACHABLE;
    }
};

template<>
inline void DateSecondPart::operation(common::timestamp_t& input, int64_t& result) {
    result = common::Timestamp::getTimestampPart(common::DatePartSpecifier::SECOND, input);
}

template<>
inline void DateSecondPart::operation(common::date_t& input, int64_t& result) {
    result = common::Date::getDatePart(common::DatePartSpecifier::SECOND, input);
}

struct AddDay {
    static inline void operation(common::timestamp_t& left, int64_t& Day,
        common::timestamp_t& result) {
        auto right = common::interval_t(0, 0, Day * common::Interval::MICROS_PER_DAY);
        result = left + right;
    }
};

struct AddHour {
    static inline void operation(common::timestamp_t& left, int64_t& Hour,
        common::timestamp_t& result) {
        auto right = common::interval_t(0, 0, Hour * common::Interval::MICROS_PER_HOUR);
        result = left + right;
    }
};

struct AddMinute {
    static inline void operation(common::timestamp_t& left, int64_t& Minute,
        common::timestamp_t& result) {
        auto right = common::interval_t(0, 0, Minute * common::Interval::MICROS_PER_MINUTE);
        result = left + right;
    }
};

struct AddSecond {
    static inline void operation(common::timestamp_t& left, int64_t& Second,
        common::timestamp_t& result) {
        auto right = common::interval_t(0, 0, Second * common::Interval::MICROS_PER_SEC);
        result = left + right;
    }
};

struct CurrentDate {
    static void operation(common::date_t& result, void* dataPtr);
};

struct CurrentTimestamp {
    static void operation(common::timestamp_tz_t& result, void* dataPtr);
};

} // namespace function
} // namespace kuzu
