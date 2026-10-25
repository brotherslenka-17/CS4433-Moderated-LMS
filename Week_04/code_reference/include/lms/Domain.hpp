#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lms {
using Id = std::int64_t;
using TimePoint = std::int64_t;
using MarkUnits = std::int64_t;
using Bytes = std::vector<unsigned char>;
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
void require(bool condition, const std::string &message);
void textRequired(const std::string &value, std::size_t maximum, const std::string &field);
std::string safeFilename(const std::string &name);
MarkUnits parseMark(const std::string &text);
std::string formatMark(MarkUnits value);
TimePoint parseLocalTime(const std::string &text);
std::string formatLocalTime(TimePoint value);

enum class Role { Student, Lecturer, Hod, Admin };
std::string roleName(Role role);
Role parseRole(const std::string &text);
class User {
  public:
    explicit User(Id id) : id_(id) {}
    virtual ~User() = default;
    virtual Role role() const = 0;
    Id id() const { return id_; }

  private:
    Id id_;
};
class Student final : public User {
  public:
    using User::User;
    Role role() const override { return Role::Student; }
};
class Lecturer final : public User {
  public:
    using User::User;
    Role role() const override { return Role::Lecturer; }
};
class HeadOfDepartment final : public User {
  public:
    using User::User;
    Role role() const override { return Role::Hod; }
};
class Administrator final : public User {
  public:
    using User::User;
    Role role() const override { return Role::Admin; }
};
std::unique_ptr<User> makeUser(Id id, Role role);
class IClock {
  public:
    virtual ~IClock() = default;
    virtual TimePoint now() const = 0;
};
class SystemClock final : public IClock {
  public:
    TimePoint now() const override;
};
class ILatePenalty {
  public:
    virtual ~ILatePenalty() = default;
    virtual MarkUnits calculate(MarkUnits raw, MarkUnits maximum, TimePoint elapsed) const = 0;
};
class TenPercentPenalty final : public ILatePenalty {
  public:
    MarkUnits calculate(MarkUnits raw, MarkUnits maximum, TimePoint elapsed) const override;
};
std::int64_t lateDays(TimePoint elapsed);
struct UserRecord {
    Id id{};
    std::string username, name, hash;
    Role role{Role::Student};
    bool active{true}, mustChange{true};
    int failed{};
    TimePoint lockedUntil{};
};
struct Department {
    Id id{};
    std::string name;
};
struct Course {
    Id id{};
    std::string code, title;
    Id department{};
    bool active{true};
};
struct Assignment {
    Id id{}, course{}, creator{};
    std::string title, instructions;
    TimePoint deadline{};
    MarkUnits maximum{};
};
struct IndividualExtension {
    Id assignment{}, student{}, grantor{};
    TimePoint deadline{}, at{};
    std::string reason;
};
struct Submission {
    Id id{}, assignment{}, student{};
    std::string phase{"RECEIVED"};
    int rowVersion{1};
};
struct SubmissionVersion {
    Id submission{};
    int number{};
    TimePoint received{}, deadlineAtReceipt{};
    std::string filename;
    Bytes content;
};
struct Assessment {
    Id submission{};
    int version{};
    MarkUnits raw{}, final{};
    std::int64_t days{};
    TimePoint deadline{};
    std::string feedback;
    Id marker{};
    int revision{};
    Id moderator{}, releasedBy{};
    TimePoint moderatedAt{}, releasedAt{};
};
struct AssessmentEvent {
    Id id{}, submission{}, actor{};
    int revision{};
    TimePoint at{};
    std::string action;
    std::optional<MarkUnits> oldRaw, oldFinal;
    MarkUnits newRaw{}, newFinal{};
    std::optional<std::string> oldFeedback;
    std::string newFeedback, oldPhase, newPhase, reason;
};
struct AssignmentView {
    Assignment assignment;
    TimePoint effective{};
    std::optional<IndividualExtension> individual;
    std::optional<Submission> submission;
    std::optional<SubmissionVersion> receipt;
};
struct WorkView {
    Submission submission;
    SubmissionVersion version;
    std::optional<Assessment> assessment;
};
struct ReleasedResult {
    Id submission{};
    std::string assignment;
    MarkUnits maximum{}, final{};
    std::string feedback;
};
struct Context {
    Id id{};
    Role role{Role::Student};
    std::string name;
};
} // namespace lms
