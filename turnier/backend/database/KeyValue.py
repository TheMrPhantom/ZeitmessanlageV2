import sqlalchemy as sql
from web import sql_database as db
from sqlalchemy.orm import relationship


class KeyValue(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    key = sql.Column(sql.String(256), nullable=False)
    value = sql.Column(sql.String(256), nullable=False)
