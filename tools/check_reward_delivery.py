#!/usr/bin/env python3
"""Check payout/marker crash gap using both modules' actual emitted SQL."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wallet', type=Path, required=True)
    parser.add_argument('--reward-binary', type=Path, required=True)
    parser.add_argument('--wallet-binary', type=Path, required=True)
    args = parser.parse_args()
    command = ['mysql', '--protocol=tcp', '-h', '127.0.0.1', '-uroot', '--batch', '--skip-column-names', 'delve_test']

    def sql(statement, success=True):
        result = subprocess.run(command, input=statement, text=True, capture_output=True)
        if (result.returncode == 0) != success:
            raise AssertionError(result.stderr)
        return result.stdout.strip()

    for name in ('mod_boss_coins_characters.sql', 'mod_boss_coins_delve_awards.sql'):
        sql((args.wallet / 'data/sql/db-characters' / name).read_text())
    # check_reward_outbox left its source run at "other" with no intent.
    insert = subprocess.check_output([str(args.reward_binary.resolve()), '--sql'], text=True)
    ack = subprocess.check_output([str(args.reward_binary.resolve()), '--ack'], text=True)
    call = subprocess.check_output([str(args.wallet_binary.resolve()), '--sql'], text=True)
    sql("UPDATE mod_delves_run SET checkpoint='done' WHERE run_id=100;" + insert)
    sql('INSERT INTO mod_boss_coins_wallet(account,coins) VALUES(7,10);')
    # Wrong identity/amount cannot consume an outstanding obligation.
    sql(ack.replace('account=7', 'account=8'))
    sql(ack.replace('coins=3', 'coins=4'))
    assert sql('SELECT delivered FROM mod_delves_reward WHERE run_id=100 AND guid=42;') == '0'
    sql(call)
    sql("CREATE TRIGGER refuse_marker BEFORE UPDATE ON mod_delves_reward FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected marker failure';")
    sql(ack, False)
    assert sql('SELECT delivered FROM mod_delves_reward WHERE run_id=100 AND guid=42;') == '0'
    assert sql('SELECT coins FROM mod_boss_coins_wallet WHERE account=7;') == '13'
    # On recovery, the API reads this exact receipt and returns AlreadyPaid;
    # its pure C++ tests cover that dispatch. Repeating the SQL cannot pay twice.
    sql(call, False)
    assert sql('SELECT CONCAT(run_id,":",account,":",guid,":",coins) FROM mod_boss_coins_delve_receipt;') == '100:7:42:3'
    sql('DROP TRIGGER refuse_marker;')
    sql(ack)
    sql(ack)
    assert sql('SELECT delivered FROM mod_delves_reward WHERE run_id=100 AND guid=42;') == '1'
    assert sql('SELECT coins FROM mod_boss_coins_wallet WHERE account=7;') == '13'
    assert sql('SELECT COUNT(*) FROM mod_boss_coins_ledger;') == '1'
    print('Both modules generated SQL preserves payout through marker failure/recovery without another credit')


if __name__ == '__main__':
    main()
