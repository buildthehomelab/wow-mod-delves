-- Runs only in the disposable CI database, never a realm database.
INSERT INTO mod_delves_run (instance_id, checkpoint) VALUES (101, 'first');
INSERT IGNORE INTO mod_delves_run (instance_id, checkpoint) VALUES (101, 'replacement');
UPDATE mod_delves_run SET checkpoint = 'second' WHERE instance_id = 101 AND BINARY checkpoint = BINARY 'first';
UPDATE mod_delves_run SET checkpoint = 'stale' WHERE instance_id = 101 AND BINARY checkpoint = BINARY 'first';
INSERT INTO mod_delves_visit VALUES (42, 7, 0, 1, 2, 3, 4, 1, 20, 1, 0);
INSERT IGNORE INTO mod_delves_visit VALUES (42, 8, 1, 9, 9, 9, 9, 3, 60, 2, 1);
UPDATE mod_delves_visit SET returning = 1 WHERE guid = 42 AND account = 8;
DELIMITER //
CREATE PROCEDURE assert_journal()
BEGIN
  IF (SELECT COUNT(*) FROM mod_delves_run WHERE instance_id = 101) != 1 OR
     (SELECT checkpoint FROM mod_delves_run WHERE instance_id = 101) != 'second' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'instance uniqueness or checkpoint comparison failed';
  END IF;
  IF (SELECT COUNT(*) FROM mod_delves_visit WHERE guid = 42 AND account = 7 AND returning = 0) != 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'return-location ownership constraint failed';
  END IF;
END//
DELIMITER ;
CALL assert_journal();
DROP PROCEDURE assert_journal;
