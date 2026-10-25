#include "lms/Domain.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
namespace lms {
void require(bool condition, const std::string &message) {
    if (!condition)
        throw Error(message);
}
void textRequired(const std::string &value, std::size_t maximum, const std::string &field) {
    require(!value.empty() && value.size() <= maximum, field + " has invalid length.");
    bool visible = false;
    for (unsigned char c : value) {
        require(c >= 32 && c != 127, field + " contains control characters.");
        if (c != ' ')
            visible = true;
    }
    require(visible, field + " cannot be blank.");
}
std::string safeFilename(const std::string &name) {
    auto pos = name.find_last_of("/\\");
    std::string leaf = pos == std::string::npos ? name : name.substr(pos + 1);
    textRequired(leaf, 200, "Filename");
    require(leaf != "." && leaf != "..", "Invalid filename.");
    return leaf;
}
MarkUnits parseMark(const std::string &text) {
    require(!text.empty() && text.size() <= 10,
            "Enter a non-negative mark with up to two decimals.");
    MarkUnits whole = 0, frac = 0;
    bool dot = false;
    int digits = 0, wholeDigits = 0;
    for (char c : text) {
        if (c == '.' && !dot) {
            dot = true;
            continue;
        }
        require(c >= '0' && c <= '9', "Invalid mark.");
        if (dot) {
            require(++digits <= 2, "At most two decimal places.");
            frac = frac * 10 + c - '0';
        } else {
            whole = whole * 10 + c - '0';
            ++wholeDigits;
            require(whole <= 1000, "Mark exceeds 1000.");
        }
    }
    require(wholeDigits > 0 && (!dot || digits > 0), "Invalid mark.");
    if (digits == 1)
        frac *= 10;
    return whole * 100 + frac;
}
std::string formatMark(MarkUnits value) {
    std::ostringstream out;
    out << value / 100 << '.' << std::setw(2) << std::setfill('0') << value % 100;
    return out.str();
}
TimePoint parseLocalTime(const std::string &text) {
    require(text.size() == 19, "Use YYYY-MM-DD HH:MM:SS in school time (UTC+02:00).");
    std::tm tm{};
    std::istringstream in(text);
    in >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    require(!in.fail() && in.peek() == std::char_traits<char>::eof(), "Invalid date/time.");
    require(tm.tm_year >= 100 && tm.tm_year <= 200, "Year must be 2000-2100.");
#ifdef _WIN32
    auto seconds = _mkgmtime(&tm);
#else
    auto seconds = timegm(&tm);
#endif
    TimePoint utc = static_cast<TimePoint>(seconds) - 7200;
    require(formatLocalTime(utc).substr(0, 19) == text, "Invalid calendar date/time.");
    return utc;
}
std::string formatLocalTime(TimePoint value) {
    std::time_t t = static_cast<std::time_t>(value + 7200);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " +02:00";
    return out.str();
}
std::string roleName(Role role) {
    switch (role) {
    case Role::Student:
        return "STUDENT";
    case Role::Lecturer:
        return "LECTURER";
    case Role::Hod:
        return "HOD";
    case Role::Admin:
        return "ADMIN";
    }
    throw Error("Unknown role.");
}
Role parseRole(const std::string &text) {
    if (text == "STUDENT")
        return Role::Student;
    if (text == "LECTURER")
        return Role::Lecturer;
    if (text == "HOD")
        return Role::Hod;
    if (text == "ADMIN")
        return Role::Admin;
    throw Error("Unknown role.");
}
std::unique_ptr<User> makeUser(Id id, Role role) {
    switch (role) {
    case Role::Student:
        return std::make_unique<Student>(id);
    case Role::Lecturer:
        return std::make_unique<Lecturer>(id);
    case Role::Hod:
        return std::make_unique<HeadOfDepartment>(id);
    case Role::Admin:
        return std::make_unique<Administrator>(id);
    }
    throw Error("Unknown role.");
}
TimePoint SystemClock::now() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::int64_t lateDays(TimePoint elapsed) {
    return elapsed <= 0 ? 0 : 1 + (elapsed - 1) / 86400;
}
MarkUnits TenPercentPenalty::calculate(MarkUnits raw, MarkUnits maximum, TimePoint elapsed) const {
    require(maximum >= 100 && maximum <= 100000, "Maximum mark must be 1.00-1000.00.");
    require(raw >= 0 && raw <= maximum, "Raw mark is outside assignment range.");
    auto days = lateDays(elapsed);
    if (days > 3)
        return 0;
    return std::max<MarkUnits>(0, raw - (maximum * days + 5) / 10);
}
} // namespace lms
