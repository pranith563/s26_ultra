SELECT name, ts, dur FROM slice WHERE name = 'S26_QNN_HTP_RUN';
SELECT name, value, severity FROM stats WHERE value > 0 AND severity != 'info';
SELECT COUNT(*) AS sched_rows FROM sched;
SELECT id, name FROM counter_track LIMIT 12;
SELECT * FROM clock_snapshot LIMIT 12;
