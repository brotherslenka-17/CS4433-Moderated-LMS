#include "Schema.hpp"
#include "lms/Repository.hpp"
#include <filesystem>
#include <utility>
namespace lms {
namespace {
class Statement {
  public:
    Statement(sqlite3 *db, const char *sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
            throw Error("Database statement could not be prepared.");
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    void bind(int index, Id value) { check(sqlite3_bind_int64(stmt_, index, value)); }
    void bind(int index, const std::string &value) {
        check(sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()),
                                SQLITE_TRANSIENT));
    }
    void bind(int index, const Bytes &value) {
        check(sqlite3_bind_blob(stmt_, index, value.data(), static_cast<int>(value.size()),
                                SQLITE_TRANSIENT));
    }
    void null(int index) { check(sqlite3_bind_null(stmt_, index)); }
    void nullable(int index, Id value) {
        if (value == 0)
            null(index);
        else
            bind(index, value);
    }
    bool row() {
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW)
            return true;
        if (rc == SQLITE_DONE)
            return false;
        throw Error(
            "Database operation failed (constraint, storage or lock); no change committed.");
    }
    void done() { require(!row(), "Unexpected database row."); }
    Id integer(int column) const { return sqlite3_column_int64(stmt_, column); }
    std::string text(int column) const {
        auto p = sqlite3_column_text(stmt_, column);
        return p ? std::string(reinterpret_cast<const char *>(p),
                               sqlite3_column_bytes(stmt_, column))
                 : "";
    }
    bool isNull(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }
    Bytes blob(int column) const {
        auto p = static_cast<const unsigned char *>(sqlite3_column_blob(stmt_, column));
        auto n = sqlite3_column_bytes(stmt_, column);
        return n > 0 ? Bytes(p, p + n) : Bytes{};
    }

  private:
    void check(int rc) {
        if (rc != SQLITE_OK)
            throw Error("Database parameter binding failed.");
    }
    sqlite3 *db_;
    sqlite3_stmt *stmt_{};
};
void exec(sqlite3 *db, const char *sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw Error("Database transaction or schema operation failed.");
}
UserRecord readUser(const Statement &s) {
    return {
        s.integer(0),         s.text(1),         s.text(2),         s.text(3),
        parseRole(s.text(4)), s.integer(5) != 0, s.integer(6) != 0, static_cast<int>(s.integer(7)),
        s.integer(8)};
}
Course readCourse(const Statement &s) {
    return {s.integer(0), s.text(1), s.text(2), s.integer(3), s.integer(4) != 0};
}
Assignment readAssignment(const Statement &s) {
    return {s.integer(0), s.integer(1), s.integer(2), s.text(3),
            s.text(4),    s.integer(5), s.integer(6)};
}
Submission readSubmission(const Statement &s) {
    return {s.integer(0), s.integer(1), s.integer(2), s.text(3), static_cast<int>(s.integer(4))};
}
} // namespace
SqliteRepository::SqliteRepository(const std::string &path) {
    int rc =
        sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw Error("Cannot open database. Check path and permissions.");
    }
    try {
        sqlite3_busy_timeout(db_, 2000);
        exec(db_, "PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL;");
        Statement fk(db_, "PRAGMA foreign_keys");
        require(fk.row() && fk.integer(0) == 1, "Foreign keys unavailable.");
        Statement integrity(db_, "PRAGMA quick_check");
        require(integrity.row() && integrity.text(0) == "ok",
                "Database integrity check failed. Stop and restore a verified backup.");
        Statement v(db_, "PRAGMA user_version");
        require(v.row(), "Schema version unavailable.");
        auto version = v.integer(0);
        require(version <= 1, "Database is newer than this application; use the matching release.");
        if (version == 0) {
            exec(db_, "BEGIN IMMEDIATE");
            try {
                exec(db_, INITIAL_SCHEMA);
                exec(db_, "PRAGMA user_version=1;");
                exec(db_, "COMMIT");
            } catch (...) {
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                throw;
            }
        }
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}
SqliteRepository::~SqliteRepository() {
    sqlite3_close(db_);
}
void SqliteRepository::begin() {
    exec(db_, "BEGIN IMMEDIATE");
}
void SqliteRepository::commit() {
    exec(db_, "COMMIT");
}
void SqliteRepository::rollback() noexcept {
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
}
int SqliteRepository::userCount() {
    Statement s(db_, "SELECT COUNT(*) FROM users");
    s.row();
    return static_cast<int>(s.integer(0));
}
int SqliteRepository::activeAdmins() {
    Statement s(db_, "SELECT COUNT(*) FROM users WHERE role='ADMIN' AND active=1");
    s.row();
    return static_cast<int>(s.integer(0));
}
std::optional<UserRecord> SqliteRepository::user(Id id) {
    Statement s(db_, "SELECT * FROM users WHERE id=?");
    s.bind(1, id);
    if (!s.row())
        return {};
    return readUser(s);
}
std::optional<UserRecord> SqliteRepository::userByName(const std::string &name) {
    Statement s(db_, "SELECT * FROM users WHERE username=?");
    s.bind(1, name);
    if (!s.row())
        return {};
    return readUser(s);
}
std::vector<UserRecord> SqliteRepository::users() {
    Statement s(db_, "SELECT * FROM users ORDER BY id");
    std::vector<UserRecord> out;
    while (s.row())
        out.push_back(readUser(s));
    return out;
}
Id SqliteRepository::addUser(const UserRecord &u) {
    Statement s(db_, "INSERT INTO "
                     "users(username,display_name,password_hash,role,active,must_change_password,"
                     "failed_attempts,locked_until) VALUES(?,?,?,?,?,?,?,?)");
    s.bind(1, u.username);
    s.bind(2, u.name);
    s.bind(3, u.hash);
    s.bind(4, roleName(u.role));
    s.bind(5, u.active);
    s.bind(6, u.mustChange);
    s.bind(7, u.failed);
    s.nullable(8, u.lockedUntil);
    s.done();
    return sqlite3_last_insert_rowid(db_);
}
void SqliteRepository::saveUser(const UserRecord &u) {
    Statement s(db_, "UPDATE users SET "
                     "display_name=?,password_hash=?,active=?,must_change_password=?,failed_"
                     "attempts=?,locked_until=? WHERE id=?");
    s.bind(1, u.name);
    s.bind(2, u.hash);
    s.bind(3, u.active);
    s.bind(4, u.mustChange);
    s.bind(5, u.failed);
    s.nullable(6, u.lockedUntil);
    s.bind(7, u.id);
    s.done();
}
std::vector<Department> SqliteRepository::departments() {
    Statement s(db_, "SELECT id,name FROM departments ORDER BY id");
    std::vector<Department> out;
    while (s.row())
        out.push_back({s.integer(0), s.text(1)});
    return out;
}
Id SqliteRepository::addDepartment(const std::string &name) {
    Statement s(db_, "INSERT INTO departments(name) VALUES(?)");
    s.bind(1, name);
    s.done();
    return sqlite3_last_insert_rowid(db_);
}
void SqliteRepository::renameDepartment(Id id, const std::string &name) {
    Statement s(db_, "UPDATE departments SET name=? WHERE id=?");
    s.bind(1, name);
    s.bind(2, id);
    s.done();
    require(sqlite3_changes(db_) == 1, "Department does not exist.");
}
void SqliteRepository::setHead(Id department, Id user) {
    Statement s(db_, "INSERT INTO department_heads VALUES(?,?) ON CONFLICT(department_id) DO "
                     "UPDATE SET hod_id=excluded.hod_id");
    s.bind(1, department);
    s.bind(2, user);
    s.done();
}
Id SqliteRepository::head(Id department) {
    Statement s(db_, "SELECT hod_id FROM department_heads WHERE department_id=?");
    s.bind(1, department);
    return s.row() ? s.integer(0) : 0;
}
std::vector<Course> SqliteRepository::courses() {
    Statement s(db_, "SELECT * FROM courses ORDER BY id");
    std::vector<Course> out;
    while (s.row())
        out.push_back(readCourse(s));
    return out;
}
Course SqliteRepository::course(Id id) {
    Statement s(db_, "SELECT * FROM courses WHERE id=?");
    s.bind(1, id);
    require(s.row(), "Course not found.");
    return readCourse(s);
}
Id SqliteRepository::addCourse(const Course &c) {
    Statement s(db_, "INSERT INTO courses(code,title,department_id,active) VALUES(?,?,?,?)");
    s.bind(1, c.code);
    s.bind(2, c.title);
    s.bind(3, c.department);
    s.bind(4, c.active);
    s.done();
    return sqlite3_last_insert_rowid(db_);
}
void SqliteRepository::saveCourse(const Course &c) {
    Statement s(db_, "UPDATE courses SET title=?,active=? WHERE id=?");
    s.bind(1, c.title);
    s.bind(2, c.active);
    s.bind(3, c.id);
    s.done();
}
void SqliteRepository::setLecturer(Id course, Id user, bool assigned) {
    Statement s(db_, assigned ? "INSERT OR IGNORE INTO lecturer_courses VALUES(?,?)"
                              : "DELETE FROM lecturer_courses WHERE course_id=? AND lecturer_id=?");
    s.bind(1, course);
    s.bind(2, user);
    s.done();
}
void SqliteRepository::setEnrolment(Id course, Id user, bool active) {
    Statement s(db_, "INSERT INTO enrolments VALUES(?,?,?) ON CONFLICT(course_id,student_id) DO "
                     "UPDATE SET active=excluded.active");
    s.bind(1, course);
    s.bind(2, user);
    s.bind(3, active);
    s.done();
}
bool SqliteRepository::teaches(Id course, Id user) {
    Statement s(db_, "SELECT 1 FROM lecturer_courses WHERE course_id=? AND lecturer_id=?");
    s.bind(1, course);
    s.bind(2, user);
    return s.row();
}
bool SqliteRepository::enrolled(Id course, Id user) {
    Statement s(db_, "SELECT 1 FROM enrolments WHERE course_id=? AND student_id=? AND active=1");
    s.bind(1, course);
    s.bind(2, user);
    return s.row();
}
std::vector<std::pair<Id, bool>> SqliteRepository::enrolments(Id course) {
    Statement s(db_,
                "SELECT student_id,active FROM enrolments WHERE course_id=? ORDER BY student_id");
    s.bind(1, course);
    std::vector<std::pair<Id, bool>> out;
    while (s.row())
        out.emplace_back(s.integer(0), s.integer(1) != 0);
    return out;
}
std::vector<Id> SqliteRepository::lecturers(Id course) {
    Statement s(db_,
                "SELECT lecturer_id FROM lecturer_courses WHERE course_id=? ORDER BY lecturer_id");
    s.bind(1, course);
    std::vector<Id> out;
    while (s.row())
        out.push_back(s.integer(0));
    return out;
}
Assignment SqliteRepository::assignment(Id id) {
    Statement s(db_, "SELECT * FROM assignments WHERE id=?");
    s.bind(1, id);
    require(s.row(), "Assignment not found.");
    return readAssignment(s);
}
std::vector<Assignment> SqliteRepository::assignments(Id course) {
    Statement s(db_, "SELECT * FROM assignments WHERE course_id=? ORDER BY id");
    s.bind(1, course);
    std::vector<Assignment> out;
    while (s.row())
        out.push_back(readAssignment(s));
    return out;
}
Id SqliteRepository::addAssignment(const Assignment &a) {
    Statement s(db_, "INSERT INTO "
                     "assignments(course_id,creator_id,title,instructions,deadline_utc,max_mark_"
                     "units) VALUES(?,?,?,?,?,?)");
    s.bind(1, a.course);
    s.bind(2, a.creator);
    s.bind(3, a.title);
    s.bind(4, a.instructions);
    s.bind(5, a.deadline);
    s.bind(6, a.maximum);
    s.done();
    return sqlite3_last_insert_rowid(db_);
}
void SqliteRepository::saveAssignment(const Assignment &a) {
    Statement s(db_, "UPDATE assignments SET deadline_utc=?,max_mark_units=? WHERE id=?");
    s.bind(1, a.deadline);
    s.bind(2, a.maximum);
    s.bind(3, a.id);
    s.done();
}
bool SqliteRepository::hasSubmissions(Id assignment) {
    Statement s(db_, "SELECT 1 FROM submissions WHERE assignment_id=? LIMIT 1");
    s.bind(1, assignment);
    return s.row();
}
std::optional<IndividualExtension> SqliteRepository::extension(Id assignment, Id student) {
    Statement s(db_, "SELECT * FROM individual_extensions WHERE assignment_id=? AND student_id=?");
    s.bind(1, assignment);
    s.bind(2, student);
    if (!s.row())
        return {};
    return IndividualExtension{s.integer(0), s.integer(1), s.integer(3),
                               s.integer(2), s.integer(4), s.text(5)};
}
void SqliteRepository::saveExtension(const IndividualExtension &e) {
    Statement s(db_, "INSERT INTO individual_extensions VALUES(?,?,?,?,?,?) ON "
                     "CONFLICT(assignment_id,student_id) DO UPDATE SET "
                     "deadline_utc=excluded.deadline_utc,granted_by=excluded.granted_by,granted_at="
                     "excluded.granted_at,reason=excluded.reason");
    s.bind(1, e.assignment);
    s.bind(2, e.student);
    s.bind(3, e.deadline);
    s.bind(4, e.grantor);
    s.bind(5, e.at);
    s.bind(6, e.reason);
    s.done();
}
std::optional<Submission> SqliteRepository::submissionFor(Id assignment, Id student) {
    Statement s(db_, "SELECT * FROM submissions WHERE assignment_id=? AND student_id=?");
    s.bind(1, assignment);
    s.bind(2, student);
    if (!s.row())
        return {};
    return readSubmission(s);
}
Submission SqliteRepository::submission(Id id) {
    Statement s(db_, "SELECT * FROM submissions WHERE id=?");
    s.bind(1, id);
    require(s.row(), "Submission not found.");
    return readSubmission(s);
}
std::vector<Submission> SqliteRepository::submissions(Id assignment) {
    Statement s(db_, "SELECT * FROM submissions WHERE assignment_id=? ORDER BY id");
    s.bind(1, assignment);
    std::vector<Submission> out;
    while (s.row())
        out.push_back(readSubmission(s));
    return out;
}
Id SqliteRepository::addSubmission(Id assignment, Id student) {
    Statement s(db_, "INSERT INTO submissions(assignment_id,student_id) VALUES(?,?)");
    s.bind(1, assignment);
    s.bind(2, student);
    s.done();
    return sqlite3_last_insert_rowid(db_);
}
void SqliteRepository::saveSubmission(const Submission &sub) {
    Statement s(db_, "UPDATE submissions SET phase=?,row_version=? WHERE id=?");
    s.bind(1, sub.phase);
    s.bind(2, sub.rowVersion);
    s.bind(3, sub.id);
    s.done();
}
SubmissionVersion SqliteRepository::latestVersion(Id submission, bool withBytes) {
    Statement s(
        db_,
        withBytes
            ? "SELECT * FROM submission_versions WHERE submission_id=? ORDER BY version_no DESC "
              "LIMIT 1"
            : "SELECT submission_id,version_no,received_at,deadline_at_receipt,filename,NULL FROM "
              "submission_versions WHERE submission_id=? ORDER BY version_no DESC LIMIT 1");
    s.bind(1, submission);
    require(s.row(), "No work version found.");
    return {s.integer(0), static_cast<int>(s.integer(1)), s.integer(2), s.integer(3), s.text(4),
            s.blob(5)};
}
void SqliteRepository::addVersion(const SubmissionVersion &v) {
    Statement s(db_, "INSERT INTO submission_versions VALUES(?,?,?,?,?,?)");
    s.bind(1, v.submission);
    s.bind(2, v.number);
    s.bind(3, v.received);
    s.bind(4, v.deadlineAtReceipt);
    s.bind(5, v.filename);
    s.bind(6, v.content);
    s.done();
}
std::optional<Assessment> SqliteRepository::assessment(Id submission) {
    Statement s(db_, "SELECT * FROM assessments WHERE submission_id=?");
    s.bind(1, submission);
    if (!s.row())
        return {};
    return Assessment{s.integer(0),
                      static_cast<int>(s.integer(1)),
                      s.integer(2),
                      s.integer(3),
                      s.integer(4),
                      s.integer(5),
                      s.text(6),
                      s.integer(7),
                      static_cast<int>(s.integer(8)),
                      s.integer(9),
                      s.integer(11),
                      s.integer(10),
                      s.integer(12)};
}
void SqliteRepository::saveAssessment(const Assessment &a) {
    Statement s(db_,
                "INSERT INTO assessments VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON "
                "CONFLICT(submission_id) DO UPDATE SET "
                "version_no=excluded.version_no,raw_units=excluded.raw_units,final_units=excluded."
                "final_units,late_days=excluded.late_days,effective_deadline=excluded.effective_"
                "deadline,feedback=excluded.feedback,marker_id=excluded.marker_id,revision="
                "excluded.revision,moderator_id=excluded.moderator_id,moderated_at=excluded."
                "moderated_at,released_by=excluded.released_by,released_at=excluded.released_at");
    s.bind(1, a.submission);
    s.bind(2, a.version);
    s.bind(3, a.raw);
    s.bind(4, a.final);
    s.bind(5, a.days);
    s.bind(6, a.deadline);
    s.bind(7, a.feedback);
    s.bind(8, a.marker);
    s.bind(9, a.revision);
    s.nullable(10, a.moderator);
    s.nullable(11, a.moderatedAt);
    s.nullable(12, a.releasedBy);
    s.nullable(13, a.releasedAt);
    s.done();
}
void SqliteRepository::addEvent(const AssessmentEvent &e) {
    Statement s(db_, "INSERT INTO "
                     "assessment_events(submission_id,revision,actor_id,occurred_at,action,old_raw_"
                     "units,new_raw_units,old_final_units,new_final_units,old_feedback,new_"
                     "feedback,old_phase,new_phase,reason) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    s.bind(1, e.submission);
    s.bind(2, e.revision);
    s.bind(3, e.actor);
    s.bind(4, e.at);
    s.bind(5, e.action);
    if (e.oldRaw)
        s.bind(6, *e.oldRaw);
    else
        s.null(6);
    s.bind(7, e.newRaw);
    if (e.oldFinal)
        s.bind(8, *e.oldFinal);
    else
        s.null(8);
    s.bind(9, e.newFinal);
    if (e.oldFeedback)
        s.bind(10, *e.oldFeedback);
    else
        s.null(10);
    s.bind(11, e.newFeedback);
    s.bind(12, e.oldPhase);
    s.bind(13, e.newPhase);
    s.bind(14, e.reason);
    s.done();
}
std::vector<AssessmentEvent> SqliteRepository::events(Id submission) {
    Statement s(db_, "SELECT * FROM assessment_events WHERE submission_id=? ORDER BY revision");
    s.bind(1, submission);
    std::vector<AssessmentEvent> out;
    while (s.row()) {
        AssessmentEvent e;
        e.id = s.integer(0);
        e.submission = s.integer(1);
        e.revision = static_cast<int>(s.integer(2));
        e.actor = s.integer(3);
        e.at = s.integer(4);
        e.action = s.text(5);
        if (!s.isNull(6))
            e.oldRaw = s.integer(6);
        e.newRaw = s.integer(7);
        if (!s.isNull(8))
            e.oldFinal = s.integer(8);
        e.newFinal = s.integer(9);
        if (!s.isNull(10))
            e.oldFeedback = s.text(10);
        e.newFeedback = s.text(11);
        e.oldPhase = s.text(12);
        e.newPhase = s.text(13);
        e.reason = s.text(14);
        out.push_back(e);
    }
    return out;
}
void SqliteRepository::adminEvent(Id actor, TimePoint at, const std::string &entity, Id key,
                                  const std::string &action, const std::string &oldValue,
                                  const std::string &newValue, const std::string &reason) {
    Statement s(db_, "INSERT INTO "
                     "admin_events(actor_id,occurred_at,entity_type,entity_key,action,old_value,"
                     "new_value,reason) VALUES(?,?,?,?,?,?,?,?)");
    s.bind(1, actor);
    s.bind(2, at);
    s.bind(3, entity);
    s.bind(4, std::to_string(key));
    s.bind(5, action);
    s.bind(6, oldValue);
    s.bind(7, newValue);
    s.bind(8, reason);
    s.done();
}
void SqliteRepository::backup(const std::string &path) {
    require(!std::filesystem::exists(path),
            "Backup destination already exists. Choose a new file.");
    sqlite3 *target = nullptr;
    int rc =
        sqlite3_open_v2(path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_close(target);
        throw Error("Cannot create backup destination.");
    }
    auto backup = sqlite3_backup_init(target, "main", db_, "main");
    if (!backup) {
        sqlite3_close(target);
        std::filesystem::remove(path);
        throw Error("Backup initialization failed.");
    }
    int step = sqlite3_backup_step(backup, -1);
    int finish = sqlite3_backup_finish(backup);
    sqlite3_close(target);
    if (step != SQLITE_DONE || finish != SQLITE_OK) {
        std::filesystem::remove(path);
        throw Error("Backup failed; destination removed.");
    }
}
} // namespace lms
