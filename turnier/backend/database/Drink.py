import sqlalchemy as sql
from web import sql_database as db
from sqlalchemy.orm import relationship
import util


class Drink(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    name = sql.Column(sql.String(100), nullable=False, unique=True)
    stock = sql.Column(sql.Integer, default=0, nullable=True)
    price = sql.Column(sql.Float, default=0, nullable=True)
    category = sql.Column(sql.String(100), nullable=False,
                          default=util.default_drink_category)

    def to_dict(self):
        return {
            "id": self.id,
            "name": self.name,
            "stock": self.stock,
            "price": self.price,
            "category": self.category
        }
