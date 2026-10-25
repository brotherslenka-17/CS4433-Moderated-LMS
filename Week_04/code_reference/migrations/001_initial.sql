-- v1.0 schema. Gate 2 approval reported by the user before implementation.
-- Each connection must enable and verify foreign_keys before transactions.
PRAGMA foreign_keys = ON;
CREATE TABLE users (
 id INTEGER PRIMARY KEY,
 username TEXT NOT NULL COLLATE NOCASE UNIQUE CHECK(length(username) BETWEEN 3 AND 64),
 display_name TEXT NOT NULL CHECK(length(trim(display_name)) BETWEEN 1 AND 120),
 password_hash TEXT NOT NULL,
 role TEXT NOT NULL CHECK(role IN ('STUDENT','LECTURER','HOD','ADMIN')),
 active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0,1)),
 must_change_password INTEGER NOT NULL DEFAULT 1 CHECK(must_change_password IN (0,1)),
 failed_attempts INTEGER NOT NULL DEFAULT 0 CHECK(failed_attempts >= 0),
 locked_until INTEGER
);
CREATE TABLE departments (
 id INTEGER PRIMARY KEY,
 name TEXT NOT NULL COLLATE NOCASE UNIQUE CHECK(length(trim(name)) BETWEEN 1 AND 120)
);
CREATE TABLE department_heads (
 department_id INTEGER PRIMARY KEY REFERENCES departments(id),
 hod_id INTEGER NOT NULL REFERENCES users(id)
);
CREATE TABLE courses (
 id INTEGER PRIMARY KEY,
 code TEXT NOT NULL COLLATE NOCASE UNIQUE,
 title TEXT NOT NULL CHECK(length(trim(title)) BETWEEN 1 AND 160),
 department_id INTEGER NOT NULL REFERENCES departments(id),
 active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0,1))
);
CREATE TABLE lecturer_courses (
 course_id INTEGER NOT NULL REFERENCES courses(id),
 lecturer_id INTEGER NOT NULL REFERENCES users(id),
 PRIMARY KEY(course_id, lecturer_id)
);
CREATE TABLE enrolments (
 course_id INTEGER NOT NULL REFERENCES courses(id),
 student_id INTEGER NOT NULL REFERENCES users(id),
 active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0,1)),
 PRIMARY KEY(course_id, student_id)
);
CREATE TABLE assignments (
 id INTEGER PRIMARY KEY,
 course_id INTEGER NOT NULL REFERENCES courses(id),
 creator_id INTEGER NOT NULL REFERENCES users(id),
 title TEXT NOT NULL CHECK(length(trim(title)) BETWEEN 1 AND 160),
 instructions TEXT NOT NULL CHECK(length(trim(instructions)) BETWEEN 1 AND 10000),
 deadline_utc INTEGER NOT NULL,
 max_mark_units INTEGER NOT NULL CHECK(max_mark_units BETWEEN 100 AND 100000),
 UNIQUE(course_id, title)
);
CREATE TABLE individual_extensions (
 assignment_id INTEGER NOT NULL REFERENCES assignments(id),
 student_id INTEGER NOT NULL REFERENCES users(id),
 deadline_utc INTEGER NOT NULL,
 granted_by INTEGER NOT NULL REFERENCES users(id),
 granted_at INTEGER NOT NULL,
 reason TEXT NOT NULL CHECK(length(trim(reason)) BETWEEN 1 AND 1000),
 PRIMARY KEY(assignment_id, student_id)
);
CREATE TABLE submissions (
 id INTEGER PRIMARY KEY,
 assignment_id INTEGER NOT NULL REFERENCES assignments(id),
 student_id INTEGER NOT NULL REFERENCES users(id),
 phase TEXT NOT NULL DEFAULT 'RECEIVED'
   CHECK(phase IN ('RECEIVED','MARKED','MODERATED','RELEASED')),
 row_version INTEGER NOT NULL DEFAULT 1 CHECK(row_version > 0),
 UNIQUE(assignment_id,student_id)
);
CREATE TABLE submission_versions (
 submission_id INTEGER NOT NULL REFERENCES submissions(id),
 version_no INTEGER NOT NULL CHECK(version_no > 0),
 received_at INTEGER NOT NULL,
 deadline_at_receipt INTEGER NOT NULL,
 filename TEXT NOT NULL CHECK(length(filename) BETWEEN 1 AND 200),
 content BLOB NOT NULL CHECK(length(content) BETWEEN 1 AND 10485760),
 PRIMARY KEY(submission_id, version_no)
);
CREATE TABLE assessments (
 submission_id INTEGER PRIMARY KEY REFERENCES submissions(id),
 version_no INTEGER NOT NULL,
 raw_units INTEGER NOT NULL CHECK(raw_units >= 0),
 final_units INTEGER NOT NULL CHECK(final_units >= 0 AND final_units <= raw_units),
 late_days INTEGER NOT NULL CHECK(late_days >= 0),
 effective_deadline INTEGER NOT NULL,
 feedback TEXT NOT NULL CHECK(length(trim(feedback)) BETWEEN 1 AND 10000),
 marker_id INTEGER NOT NULL REFERENCES users(id),
 revision INTEGER NOT NULL CHECK(revision > 0),
 moderator_id INTEGER REFERENCES users(id),
 moderated_at INTEGER,
 released_by INTEGER REFERENCES users(id),
 released_at INTEGER,
 FOREIGN KEY(submission_id,version_no) REFERENCES submission_versions(submission_id,version_no),
 CHECK((moderator_id IS NULL) = (moderated_at IS NULL)),
 CHECK((released_by IS NULL) = (released_at IS NULL)),
 CHECK(released_at IS NULL OR moderated_at IS NOT NULL)
);
CREATE TABLE assessment_events (
 id INTEGER PRIMARY KEY,
 submission_id INTEGER NOT NULL REFERENCES submissions(id),
 revision INTEGER NOT NULL CHECK(revision > 0),
 actor_id INTEGER NOT NULL REFERENCES users(id),
 occurred_at INTEGER NOT NULL,
 action TEXT NOT NULL CHECK(action IN ('MARK','REVISE','MODERATE','RETURN','RELEASE','REOPEN')),
 old_raw_units INTEGER,
 new_raw_units INTEGER NOT NULL CHECK(new_raw_units >= 0),
 old_final_units INTEGER,
 new_final_units INTEGER NOT NULL CHECK(new_final_units >= 0),
 old_feedback TEXT,
 new_feedback TEXT NOT NULL,
 old_phase TEXT NOT NULL CHECK(old_phase IN ('RECEIVED','MARKED','MODERATED','RELEASED')),
 new_phase TEXT NOT NULL CHECK(new_phase IN ('RECEIVED','MARKED','MODERATED','RELEASED')),
 reason TEXT NOT NULL CHECK(length(trim(reason)) BETWEEN 1 AND 1000),
 UNIQUE(submission_id,revision)
);
CREATE TABLE admin_events (
 id INTEGER PRIMARY KEY,
 actor_id INTEGER NOT NULL REFERENCES users(id),
 occurred_at INTEGER NOT NULL,
 entity_type TEXT NOT NULL,
 entity_key TEXT NOT NULL,
 action TEXT NOT NULL,
 old_value TEXT,
 new_value TEXT NOT NULL,
 reason TEXT NOT NULL CHECK(length(trim(reason)) BETWEEN 1 AND 1000)
);
CREATE INDEX idx_courses_department ON courses(department_id);
CREATE INDEX idx_assignments_course ON assignments(course_id);
CREATE INDEX idx_enrolments_student ON enrolments(student_id,active);
CREATE INDEX idx_lecturer_courses_lecturer ON lecturer_courses(lecturer_id);
CREATE INDEX idx_heads_user ON department_heads(hod_id);
CREATE INDEX idx_submissions_assignment ON submissions(assignment_id,phase);
CREATE INDEX idx_submissions_student ON submissions(student_id);
CREATE INDEX idx_assessment_events_actor ON assessment_events(actor_id,occurred_at);
-- Append-only under normal application use; OS-level DB owners can bypass these.
CREATE TRIGGER protect_assessment_events_update BEFORE UPDATE ON assessment_events
 BEGIN SELECT RAISE(ABORT,'assessment history is append-only'); END;
CREATE TRIGGER protect_assessment_events_delete BEFORE DELETE ON assessment_events
 BEGIN SELECT RAISE(ABORT,'assessment history is append-only'); END;
CREATE TRIGGER protect_admin_events_update BEFORE UPDATE ON admin_events
 BEGIN SELECT RAISE(ABORT,'administrative history is append-only'); END;
CREATE TRIGGER protect_admin_events_delete BEFORE DELETE ON admin_events
 BEGIN SELECT RAISE(ABORT,'administrative history is append-only'); END;
CREATE TRIGGER protect_versions_update BEFORE UPDATE ON submission_versions
 BEGIN SELECT RAISE(ABORT,'submission versions are immutable'); END;
CREATE TRIGGER protect_versions_delete BEFORE DELETE ON submission_versions
 BEGIN SELECT RAISE(ABORT,'submission versions are immutable'); END;
