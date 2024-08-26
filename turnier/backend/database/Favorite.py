import sqlalchemy as sql
from web import sql_database as db
from sqlalchemy.orm import relationship


class Favorite(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    member_id = sql.Column(sql.Integer, sql.ForeignKey(
        'member.id', ondelete='SET NULL'))
    member = relationship(
        'database.Member.Member', lazy="joined")

    drink_id = sql.Column(sql.Integer, sql.ForeignKey(
        'drink.id', ondelete='SET NULL'))
    drink = relationship(
        'database.Drink.Drink', lazy="joined")

    def to_dict(self):
        return {
            "id": self.id,
            "member_id": self.member_id,
            "drink_id": self.drink_id,
        }
