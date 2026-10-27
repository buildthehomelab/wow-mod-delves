-- Development schema only; apply to an isolated test characters database.
-- Keep these rows across restarts, backup and restore with the instance table.
CREATE TABLE IF NOT EXISTS `mod_delves_run` (
  `run_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `instance_id` INT UNSIGNED NOT NULL,
  `checkpoint` MEDIUMTEXT CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  PRIMARY KEY (`run_id`),
  UNIQUE KEY `instance_id` (`instance_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `mod_delves_visit` (
  `guid` INT UNSIGNED NOT NULL,
  `account` INT UNSIGNED NOT NULL,
  `source_map` INT UNSIGNED NOT NULL,
  `source_x` FLOAT NOT NULL,
  `source_y` FLOAT NOT NULL,
  `source_z` FLOAT NOT NULL,
  `source_o` FLOAT NOT NULL,
  `tier` INT UNSIGNED NOT NULL,
  `level` INT UNSIGNED NOT NULL,
  `revision` INT UNSIGNED NOT NULL,
  `returning` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB;
