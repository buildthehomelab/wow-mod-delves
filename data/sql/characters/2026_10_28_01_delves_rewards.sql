-- Persistent completion intent. Back up and retain with run and wallet receipts.
-- Applying this schema does not enable or deliver coin rewards.
CREATE TABLE IF NOT EXISTS `mod_delves_reward` (
  `run_id` BIGINT UNSIGNED NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  `account` INT UNSIGNED NOT NULL,
  `coins` INT UNSIGNED NOT NULL,
  `policy` INT UNSIGNED NOT NULL,
  `delivered` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`run_id`,`guid`),
  KEY `pending_member` (`guid`,`delivered`)
) ENGINE=InnoDB;
