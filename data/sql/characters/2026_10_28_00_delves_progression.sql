-- Isolated test characters database only. Existing visits remain unknown, never
-- silently interpreted as Classic. Return/escape remains available for them.
SET @delve_progression_migration = IF(
    EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema = DATABASE()
        AND table_name = 'mod_delves_visit' AND column_name = 'progression'),
    'SELECT 1',
    'ALTER TABLE mod_delves_visit ADD COLUMN progression INT UNSIGNED NOT NULL DEFAULT 4294967295');
PREPARE delve_progression_migration FROM @delve_progression_migration;
EXECUTE delve_progression_migration;
DEALLOCATE PREPARE delve_progression_migration;
