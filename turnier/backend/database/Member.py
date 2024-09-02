import sqlalchemy as sql
from web import sql_database as db
from sqlalchemy.orm import relationship
import util


class Member(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    name = sql.Column(sql.String(100), nullable=False, unique=True)
    alias = sql.Column(sql.String(100), nullable=False, default="")
    balance = sql.Column(sql.Float, default=0, nullable=True)
    hidden = sql.Column(sql.Boolean, nullable=False, default=False)
    password = sql.Column(sql.LargeBinary(length=128), nullable=False)
    salt = sql.Column(sql.String(64), nullable=False)
    verified_until = sql.Column(sql.DateTime, nullable=True)

    def to_dict(self):
        return {
            "id": self.id,
            "name": self.name,
            "balance": self.balance,
            "hidden": self.hidden,
            "alias": self.alias if util.use_alias else ""
        }

    def to_dict_with_password(self):
        return {
            "id": self.id,
            "name": self.name,
            "balance": self.balance,
            "hidden": self.hidden,
            "alias": self.alias if util.use_alias else "",
            "password": self.password.hex(),
            "salt": self.salt
        }

    
