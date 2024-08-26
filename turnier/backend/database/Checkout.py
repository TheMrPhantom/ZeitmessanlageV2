import sqlalchemy as sql
from database.Transaction import Transaction
from web import sql_database as db
from sqlalchemy.orm import relationship
from datetime import datetime


class Checkout(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    transactions = relationship(
        'database.Transaction.Transaction', lazy="joined")
    date = sql.Column(sql.DateTime, default=datetime.now, nullable=False)
    current_cash = sql.Column(sql.Float, nullable=True)

    def dict(self):
        return {"id": self.id, "date": self.date.strftime('%Y-%m-%dT%H:%M:%SZ'), "currentCash": self.current_cash}

    def dict_expanded(self):
        transactions = []
        for t in self.transactions:
            transaction: Transaction = t
            transactions.append(transaction.to_dict())
        return {
            "id": self.id,
            "date": self.date.strftime('%Y-%m-%dT%H:%M:%SZ'),
            "transactions": transactions
        }
