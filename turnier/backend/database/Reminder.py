import sqlalchemy as sql
from web import sql_database as db
from sqlalchemy.orm import relationship
import json


class Reminder(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    member_id = sql.Column(sql.Integer, sql.ForeignKey(
        'member.id', ondelete='SET NULL'), nullable=True)
    member = relationship(
        'database.Member.Member', lazy="joined")

    text = sql.Column(sql.String(256), nullable=False)
    request = sql.Column(sql.String(256), nullable=True)
    member_name_from = sql.Column(sql.String(256), nullable=True)
    emoji = sql.Column(sql.String(1), nullable=True)

    def to_dict(self):
        output = {"text": self.text, "memberNameFrom": self.member_name_from,
                  "emoji": self.emoji, "request": None, "id": self.id}
        if self.request is not None:
            output["request"] = json.loads(self.request)

        return output
