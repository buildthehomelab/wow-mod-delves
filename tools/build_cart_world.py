#!/usr/bin/env python3
"""Render an additive isolated-world cart extension; never connect to a database."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = ROOT / 'test-package/burning-cart.json'
BASE = ROOT / 'test-package/earthcrawl.json'


def render():
    raw = SPEC.read_bytes()
    spec = json.loads(raw)
    base = json.loads(BASE.read_bytes())
    expected = {'package': 'earthcrawl-burning-cart-v1', 'base_package': base['package'],
                'base_manifest_sha256': hashlib.sha256(BASE.read_bytes()).hexdigest(),
                'content_revision': 2026102803, 'map': 890, 'cart_entry': 1499002,
                'cart_display': 149990, 'cart_model': 149990, 'impact_spell': 20832,
                'visual_spell': 28330, 'lane': [[-791.20508, -330.61328, 76.4421],
                    [-773.48242, -307.92969, 76.4466]], 'warning_ms': 2000, 'speed': 8,
                'radius': 2.5, 'vertical_tolerance': 2, 'damage_bolt_multiplier': 2, 'knockback': [8, 3]}
    if any(spec.get(key) != value for key, value in expected.items()):
        raise ValueError('Cart identity or required base package differs')
    name, entry, display = spec['package'], spec['cart_entry'], spec['cart_display']
    digest = hashlib.sha256(raw).hexdigest()
    marker = f"package='{name}' AND manifest_sha256='{digest}'"
    guard = f"""    IF RIGHT(DATABASE(),11) <> '_delve_test' THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Use an isolated database ending in _delve_test';
    END IF;
    IF (SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE()
        AND table_name IN ('creature','creature_template','creature_template_model','mod_delves_world_package')
        AND engine='InnoDB') <> 4 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Cart extension requires complete InnoDB world tables';
    END IF;
    START TRANSACTION;
    IF NOT EXISTS (SELECT 1 FROM mod_delves_world_package WHERE package='{base['package']}'
        AND manifest_sha256='{expected['base_manifest_sha256']}') THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Matching installed base pilot receipt required';
    END IF;
"""
    actors = f"""EXISTS (SELECT 1 FROM creature WHERE id1={entry} OR id2={entry} OR id3={entry})"""
    install = guard + f"""    IF EXISTS (SELECT 1 FROM mod_delves_world_package WHERE package='{name}')
        OR {actors}
        OR EXISTS (SELECT 1 FROM creature_template WHERE entry={entry})
        OR EXISTS (SELECT 1 FROM creature_template_model WHERE CreatureID={entry} OR CreatureDisplayID={display}) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Cart identity collision or already installed; no content replaced';
    END IF;
    INSERT INTO creature_template
        (entry,name,subname,minlevel,maxlevel,faction,unit_class,type,HealthModifier,DamageModifier,ExperienceModifier,ScriptName)
        VALUES ({entry},'Burning Cart','Earthcrawl controlled test',30,30,35,1,10,1,0,0,'npc_evermore_burning_cart');
    INSERT INTO creature_template_model (CreatureID,Idx,CreatureDisplayID,DisplayScale,Probability,VerifiedBuild)
        VALUES ({entry},0,{display},1,1,0);
    INSERT INTO mod_delves_world_package (package,manifest_sha256) VALUES ('{name}','{digest}');
"""
    rollback = guard + f"""    IF NOT EXISTS (SELECT 1 FROM mod_delves_world_package WHERE {marker}) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Matching cart receipt required; rollback refused';
    END IF;
    IF {actors}
        OR NOT EXISTS (SELECT 1 FROM creature_template WHERE entry={entry} AND ScriptName='npc_evermore_burning_cart'
            AND name='Burning Cart' AND subname='Earthcrawl controlled test'
            AND minlevel=30 AND maxlevel=30 AND faction=35 AND unit_class=1 AND type=10
            AND HealthModifier=1 AND DamageModifier=0 AND ExperienceModifier=0)
        OR (SELECT COUNT(*) FROM creature_template_model WHERE CreatureID={entry}) <> 1
        OR NOT EXISTS (SELECT 1 FROM creature_template_model WHERE CreatureID={entry} AND Idx=0
            AND CreatureDisplayID={display} AND DisplayScale=1 AND Probability=1)
        OR EXISTS (SELECT 1 FROM creature_template_model WHERE CreatureDisplayID={display} AND CreatureID<>{entry}) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Unexpected cart actor or template edits; rollback refused';
    END IF;
    DELETE FROM creature_template_model WHERE CreatureID={entry};
    DELETE FROM creature_template WHERE entry={entry};
    DELETE FROM mod_delves_world_package WHERE {marker};
"""

    def wrap(body, operation):
        procedure = 'evermore_delve_cart_' + operation + '_v1'
        return f"""-- Generated offline. Stop the isolated test world before applying.
DELIMITER //
CREATE PROCEDURE {procedure}()
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;
{body}    COMMIT;
END//
DELIMITER ;
CALL {procedure}();
DROP PROCEDURE {procedure};
"""
    return wrap(install, 'install'), wrap(rollback, 'rollback')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output must be a new directory')
    install, rollback = render()
    args.output.mkdir(parents=True)
    (args.output / 'world.install.sql').write_text(install)
    (args.output / 'world.rollback.sql').write_text(rollback)


if __name__ == '__main__':
    main()
