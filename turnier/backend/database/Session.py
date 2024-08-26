import sqlalchemy as sql
from web import sql_database as db
from sqlalchemy.orm import relationship


class Session(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    token = sql.Column(sql.String(100), nullable=False, unique=True)
    member_id = sql.Column(
        sql.Integer, sql.ForeignKey('member.id', ondelete='SET NULL'), nullable=True)
    member = relationship('database.Member.Member', lazy="joined")
    time = sql.Column(sql.DateTime, nullable=False)

    def to_dict(self):
        return (self.token, (self.member_id, self.time))
