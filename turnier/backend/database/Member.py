import sqlalchemy as sql
from web import sql_database as db
from sqlalchemy.orm import relationship
import util
import datetime


class Member(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    name = sql.Column(sql.String(100), nullable=False, unique=True)
    alias = sql.Column(sql.String(100), nullable=False, default="")
    password = sql.Column(sql.LargeBinary(length=128), nullable=False)
    salt = sql.Column(sql.String(64), nullable=False)
    verified_until = sql.Column(sql.DateTime,default=datetime.datetime.now, nullable=False)
    reference=sql.Column(sql.String,default="")

    def to_dict(self):
        return {
            "id": self.id,
            "name": self.name,
            "alias": self.alias if util.use_alias else "",
            "verifiedUntil": self.verified_until.isoformat(),
            "reference": self.reference
        }

    def to_dict_with_password(self):
        return {
            "id": self.id,
            "name": self.name,
            "alias": self.alias if util.use_alias else "",
            "password": self.password.hex(),
            "salt": self.salt
        }

    
