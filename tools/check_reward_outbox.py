#!/usr/bin/env python3
"""Exercise actual generated completion intent SQL in disposable delve_test only."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    command = ['mysql', '--protocol=tcp', '-h', '127.0.0.1', '-uroot', '--batch', '--skip-column-names', 'delve_test']

    def sql(statement, success=True):
        result = subprocess.run(command, input=statement, text=True, capture_output=True)
        if (result.returncode == 0) != success:
            raise AssertionError(result.stderr)
        return result.stdout.strip()

    insert = subprocess.check_output([str(args.binary.resolve()), '--sql'], text=True)
    sql("DELETE FROM mod_delves_run WHERE instance_id=101; INSERT INTO mod_delves_run(run_id,instance_id,checkpoint) VALUES(100,101,'active');")
    sql("START TRANSACTION; UPDATE mod_delves_run SET checkpoint='done' WHERE run_id=100 AND checkpoint='active';" + insert + 'COMMIT;')
    assert sql('SELECT CONCAT(run_id,":",guid,":",account,":",coins,":",policy,":",delivered) FROM mod_delves_reward;') == '100:42:7:3:1:0'
    sql('START TRANSACTION; ' + insert + 'COMMIT;')
    assert sql('SELECT COUNT(*) FROM mod_delves_reward;') == '1'
    # A stale CAS cannot create reward intent for an uncommitted desired state.
    sql("DELETE FROM mod_delves_reward; UPDATE mod_delves_run SET checkpoint='other' WHERE run_id=100;")
    sql("START TRANSACTION; UPDATE mod_delves_run SET checkpoint='done' WHERE run_id=100 AND checkpoint='active';" + insert + 'COMMIT;')
    assert sql('SELECT COUNT(*) FROM mod_delves_reward;') == '0'
    # Failure between checkpoint and intent must roll back both. Closing the
    # failed transaction connection mirrors core's rollback on statement failure.
    sql("CREATE TRIGGER refuse_intent BEFORE INSERT ON mod_delves_reward FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected intent failure';")
    sql("START TRANSACTION; UPDATE mod_delves_run SET checkpoint='done' WHERE run_id=100;" + insert + 'COMMIT;', False)
    assert sql('SELECT COUNT(*) FROM mod_delves_reward;') == '0'
    assert sql('SELECT checkpoint FROM mod_delves_run WHERE run_id=100;') == 'other'
    sql('DROP TRIGGER refuse_intent;')
    print('Generated completion outbox atomicity, duplicate retry and stale CAS checks passed')


if __name__ == '__main__':
    main()
