from flask_sqlalchemy import SQLAlchemy
from database.Models import *
from sqlalchemy.orm import session
from sqlalchemy import text
import util


def migrate(db: session):
    current_db_version: KeyValue = db.query(KeyValue).filter_by(
        key="version").first()
    print("Current database version:", current_db_version.value)

    migrations = [
        # Add lists for migrations
        # E.g ALTER TABLE drink ADD column price6 float DEFAULT 50
        # ["ALTER TABLE drink ADD column price6 float DEFAULT 50"]
        ["ALTER TABLE participant ADD column is_youth boolean DEFAULT FALSE"],
        ["ALTER TABLE RunInformation ADD column isGame boolean DEFAULT FALSE"],
        ["ALTER TABLE Result ADD COLUMN type INTEGER",
            "UPDATE Result SET type = CASE WHEN run % 2 = 0 THEN 0 ELSE 1 END",
            "ALTER TABLE Result DROP COLUMN run"],
    ]

    if util.CURRENT_VERSION != len(migrations):
        print("Error: No migration available")
        exit()

    for migration in migrations[int(current_db_version.value):]:
        print("Migrating from", current_db_version.value,
              "to", int(current_db_version.value)+1)
        for statement in migration:
            db.execute(text(statement))
        current_db_version.value = int(current_db_version.value)+1
        db.commit()

    print("Migrations complete")
