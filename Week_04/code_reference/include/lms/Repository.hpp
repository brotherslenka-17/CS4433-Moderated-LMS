#pragma once
#include "lms/Domain.hpp"
#include <sqlite3.h>

namespace lms {
// Domain-facing persistence port. SQL appears only in the infrastructure adapter.
class IRepository {
  public:
    virtual ~IRepository() = default;
    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() noexcept = 0;
    virtual int userCount() = 0;
    virtual int activeAdmins() = 0;
    virtual std::optional<UserRecord> user(Id id) = 0;
    virtual std::optional<UserRecord> userByName(const std::string &name) = 0;
    virtual std::vector<UserRecord> users() = 0;
    virtual Id addUser(const UserRecord &user) = 0;
    virtual void saveUser(const UserRecord &user) = 0;
    virtual std::vector<Department> departments() = 0;
    virtual Id addDepartment(const std::string &name) = 0;
    virtual void renameDepartment(Id id, const std::string &name) = 0;
    virtual void setHead(Id department, Id user) = 0;
    virtual Id head(Id department) = 0;
    virtual std::vector<Course> courses() = 0;
    virtual Course course(Id id) = 0;
    virtual Id addCourse(const Course &course) = 0;
    virtual void saveCourse(const Course &course) = 0;
    virtual void setLecturer(Id course, Id user, bool assigned) = 0;
    virtual void setEnrolment(Id course, Id user, bool active) = 0;
    virtual bool teaches(Id course, Id user) = 0;
    virtual bool enrolled(Id course, Id user) = 0;
    virtual std::vector<std::pair<Id, bool>> enrolments(Id course) = 0;
    virtual std::vector<Id> lecturers(Id course) = 0;
    virtual Assignment assignment(Id id) = 0;
    virtual std::vector<Assignment> assignments(Id course) = 0;
    virtual Id addAssignment(const Assignment &assignment) = 0;
    virtual void saveAssignment(const Assignment &assignment) = 0;
    virtual bool hasSubmissions(Id assignment) = 0;
    virtual std::optional<IndividualExtension> extension(Id assignment, Id student) = 0;
    virtual void saveExtension(const IndividualExtension &extension) = 0;
    virtual std::optional<Submission> submissionFor(Id assignment, Id student) = 0;
    virtual Submission submission(Id id) = 0;
    virtual std::vector<Submission> submissions(Id assignment) = 0;
    virtual Id addSubmission(Id assignment, Id student) = 0;
    virtual void saveSubmission(const Submission &submission) = 0;
    virtual SubmissionVersion latestVersion(Id submission, bool withBytes) = 0;
    virtual void addVersion(const SubmissionVersion &version) = 0;
    virtual std::optional<Assessment> assessment(Id submission) = 0;
    virtual void saveAssessment(const Assessment &assessment) = 0;
    virtual void addEvent(const AssessmentEvent &event) = 0;
    virtual std::vector<AssessmentEvent> events(Id submission) = 0;
    virtual void adminEvent(Id actor, TimePoint at, const std::string &entity, Id key,
                            const std::string &action, const std::string &oldValue,
                            const std::string &newValue, const std::string &reason) = 0;
    virtual void backup(const std::string &path) = 0;
};
class UnitOfWork {
  public:
    explicit UnitOfWork(IRepository &repo) : repo_(repo) { repo_.begin(); }
    ~UnitOfWork() {
        if (!committed_)
            repo_.rollback();
    }
    UnitOfWork(const UnitOfWork &) = delete;
    UnitOfWork &operator=(const UnitOfWork &) = delete;
    void commit() {
        repo_.commit();
        committed_ = true;
    }

  private:
    IRepository &repo_;
    bool committed_{};
};
class SqliteRepository : public IRepository {
  public:
    explicit SqliteRepository(const std::string &path);
    ~SqliteRepository() override;
    SqliteRepository(const SqliteRepository &) = delete;
    SqliteRepository &operator=(const SqliteRepository &) = delete;
    void begin() override;
    void commit() override;
    void rollback() noexcept override;
    int userCount() override;
    int activeAdmins() override;
    std::optional<UserRecord> user(Id id) override;
    std::optional<UserRecord> userByName(const std::string &name) override;
    std::vector<UserRecord> users() override;
    Id addUser(const UserRecord &user) override;
    void saveUser(const UserRecord &user) override;
    std::vector<Department> departments() override;
    Id addDepartment(const std::string &name) override;
    void renameDepartment(Id id, const std::string &name) override;
    void setHead(Id department, Id user) override;
    Id head(Id department) override;
    std::vector<Course> courses() override;
    Course course(Id id) override;
    Id addCourse(const Course &course) override;
    void saveCourse(const Course &course) override;
    void setLecturer(Id course, Id user, bool assigned) override;
    void setEnrolment(Id course, Id user, bool active) override;
    bool teaches(Id course, Id user) override;
    bool enrolled(Id course, Id user) override;
    std::vector<std::pair<Id, bool>> enrolments(Id course) override;
    std::vector<Id> lecturers(Id course) override;
    Assignment assignment(Id id) override;
    std::vector<Assignment> assignments(Id course) override;
    Id addAssignment(const Assignment &assignment) override;
    void saveAssignment(const Assignment &assignment) override;
    bool hasSubmissions(Id assignment) override;
    std::optional<IndividualExtension> extension(Id assignment, Id student) override;
    void saveExtension(const IndividualExtension &extension) override;
    std::optional<Submission> submissionFor(Id assignment, Id student) override;
    Submission submission(Id id) override;
    std::vector<Submission> submissions(Id assignment) override;
    Id addSubmission(Id assignment, Id student) override;
    void saveSubmission(const Submission &submission) override;
    SubmissionVersion latestVersion(Id submission, bool withBytes) override;
    void addVersion(const SubmissionVersion &version) override;
    std::optional<Assessment> assessment(Id submission) override;
    void saveAssessment(const Assessment &assessment) override;
    void addEvent(const AssessmentEvent &event) override;
    std::vector<AssessmentEvent> events(Id submission) override;
    void adminEvent(Id actor, TimePoint at, const std::string &entity, Id key,
                    const std::string &action, const std::string &oldValue,
                    const std::string &newValue, const std::string &reason) override;
    void backup(const std::string &path) override;

  private:
    sqlite3 *db_{};
};
} // namespace lms
