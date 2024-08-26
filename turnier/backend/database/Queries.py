import json
from operator import truediv
from unicodedata import name
import requests
from authenticator import TokenManager
import util
from datetime import datetime, timedelta
import os
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy.orm import session
from sqlalchemy.sql import func
from database.Models import *
from sqlalchemy import desc
from typing import List
import constants
import os
from sqlalchemy import extract
from difflib import SequenceMatcher
from sqlalchemy import inspect, text
import database.Migrations


class Queries:
    def __init__(self, db):

        self.db: SQLAlchemy = db
        self.session: session.Session = self.db.session
        self.db.create_all()
        if self.session.query(Member).first() is None:
            self.create_dummy_data()
        database.Migrations.migrate(self.session)

    def get_users(self):
        members = self.session.query(Member).all()

        output = []
        for m in members:
            member: Member = m
            if member.id > 2:
                output.append(member.to_dict())

        return output

    def get_user_favorites(self, member_id):
        favorites = self.session.query(
            Favorite).filter_by(member_id=member_id).all()
        output = []
        for f in favorites:
            favorite: Favorite = f
            output.append(favorite.drink_id)

        return output

    def get_user_history(self, member_id):
        history = self.session.query(
            Transaction).filter_by(member_id=member_id)
        output = []
        for e in history:
            entry: Transaction = e
            output.append(entry.to_dict())

        output.sort(key=lambda transaction: transaction['id'])
        output.reverse()

        return output

    def add_user_favorite(self, member_id, drink_id):
        self.session.add(Favorite(member_id=member_id, drink_id=drink_id))
        self.session.commit()

    def remove_user_favorite(self, member_id, drink_id):
        favorite: Favorite = self.session.query(Favorite).filter_by(
            member_id=member_id, drink_id=drink_id).first()
        if favorite is not None:
            self.session.delete(favorite)
            self.session.commit()

    def change_user_password(self, member_id, new_password):
        pw_hash, salt = TokenManager.hashPassword(new_password)
        user: Member = self.session.query(
            Member).filter_by(id=member_id).first()
        user.password = pw_hash
        user.salt = salt
        self.session.commit()

    def change_user_visibility(self, member_id, visibility=None):
        user: Member = self.session.query(
            Member).filter_by(id=int(member_id)).first()
        user.hidden = not user.hidden if visibility is None else visibility
        print(user.name, user.hidden, visibility)
        self.session.commit()

    def add_user(self, name, money, password, alias="", hidden=False):
        pw_hash, salt = TokenManager.hashPassword(password)
        new_member = Member(name=name.lower(), balance=money,
                            password=pw_hash, salt=salt, alias=alias, hidden=hidden)
        self.session.add(new_member)
        self.session.commit()
        return new_member

    def get_drinks(self):
        drinks = self.session.query(Drink).all()
        output = []
        for d in drinks:
            drink: Drink = d
            output.append(drink.to_dict())

        return output

    def change_drink_price(self, drink_id, price):
        drink: Drink = self.session.query(Drink).filter_by(id=drink_id).first()
        drink.price = price
        self.session.commit()

    def change_drink_stock(self, drink_id, stock, is_increase=False):
        drink: Drink = self.session.query(Drink).filter_by(id=drink_id).first()
        if not is_increase:
            drink.stock = stock
        else:
            drink.stock += stock
        self.session.commit()

    def change_drink_name(self, drink_id, name):
        drink: Drink = self.session.query(Drink).filter_by(id=drink_id).first()
        drink.name = name
        self.session.commit()

    def change_drink_category(self, drink_id, category):
        drink: Drink = self.session.query(Drink).filter_by(id=drink_id).first()
        drink.category = category
        self.session.commit()

    def delete_drink(self, drink_id):
        drink: Drink = self.session.query(Drink).filter_by(id=drink_id).first()
        self.session.delete(drink)
        self.session.commit()

    def add_drink(self, name, price, stock, category=None):
        if category is None:
            self.session.add(Drink(name=name, stock=stock, price=price))
        else:
            self.session.add(Drink(name=name, stock=stock,
                                   price=price, category=category))

        self.session.commit()

    def buy_drink(self, member_id, drink_id):
        drink: Drink = self.session.query(Drink).filter_by(id=drink_id).first()
        if drink is None:
            return {}
        member: Member = self.session.query(
            Member).filter_by(id=member_id).first()

        member.balance -= drink.price
        drink.stock -= 1
        self.session.add(Transaction(
            description=f"{drink.name}", member_id=member.id, amount=(-drink.price)))

        self.session.commit()

        return drink.to_dict()

    def get_drink_categories(self):
        drinks = self.session.query(Drink).all()
        output = []

        for d in drinks:
            drink: Drink = d
            if drink.category not in output:
                output.append(drink.category)

        return output

    def get_transactions(self, limit=None):
        transactions = []
        if limit is not None:
            transactions = self.session.query(Transaction).order_by(
                desc(Transaction.date)).limit(limit).all()
        else:
            transactions = self.session.query(
                Transaction).order_by(desc(Transaction.date)).all()
        output = []
        for t in transactions:
            transaction: Transaction = t
            output.append(transaction.to_dict())

        return output

    def get_transaction(self, transaction_id) -> dict:
        return self.session.query(Transaction).filter_by(id=transaction_id).first().to_dict()

    def delete_transaction(self, transaction_id):
        transaction: Transaction = self.session.query(
            Transaction).filter_by(id=transaction_id).first()
        member: Member = self.session.query(Member).filter_by(
            id=transaction.member_id).first()
        member.balance -= transaction.amount

        if transaction.connected_transaction_id is not None:
            transaction_connected: Transaction = self.session.query(
                Transaction).filter_by(id=transaction.connected_transaction_id).first()

            member_connected: Member = self.session.query(Member).filter_by(
                id=transaction_connected.member_id).first()
            member_connected.balance -= transaction_connected.amount
            self.session.delete(transaction_connected)

        self.session.delete(transaction)

        self.session.commit()

    def delete_user(self, member_id):
        self.session.delete(self.session.query(
            Member).filter_by(id=member_id).first())
        self.session.commit()

    def deposit_user(self, member_id, amount):
        member: Member = self.session.query(
            Member).filter_by(id=member_id).first()
        member.balance += amount
        self.session.add(Transaction(description=f"Deposit",
                                     member_id=member_id, amount=amount))
        self.session.commit()

    def get_checkouts(self):
        checkouts = self.session.query(Checkout).all()
        output = []
        for c in checkouts:
            checkout: Checkout = c
            output.append(checkout.dict())
        return output

    def get_checkout_expanded(self, id):
        checkout: Checkout = self.session.query(
            Checkout).filter_by(id=id).first()

        return checkout.dict_expanded()

    def do_checkout(self, checkouts):
        checkout = None
        if checkouts['newCash'] is not None:
            checkout: Checkout = Checkout(current_cash=checkouts['newCash'])
        else:
            db_checkouts: Checkout = self.session.query(Checkout).all()
            last_db_checkout_cash = None
            if len(db_checkouts) > 0:
                last_db_checkout_cash = db_checkouts[-1].current_cash
            else:
                last_db_checkout_cash = 0

            sum_members = 0
            for c in checkouts['members']:
                sum_members += c['amount']

            sum_invoice = 0
            for c in checkouts['invoices']:
                sum_invoice += c['amount']

            last_db_checkout_cash += sum_members
            last_db_checkout_cash -= sum_invoice

            checkout: Checkout = Checkout(current_cash=last_db_checkout_cash)
        self.session.add(checkout)
        self.session.commit()

        for c in checkouts['members']:
            member_id = c["memberID"]
            amount = c["amount"]

            member: Member = self.session.query(
                Member).filter_by(id=member_id).first()
            member.balance += amount

            self.session.add(Transaction(
                description="Checkout", member_id=member_id, amount=amount, checkout_id=checkout.id))

        for c in checkouts['invoices']:
            amount = c["amount"]
            name = c["name"]
            self.session.add(Transaction(
                description=name, member_id=1, amount=amount, checkout_id=checkout.id))

        self.session.commit()

    def get_checkout_mail(self):
        members: list[Member] = self.session.query(Member).all()
        checkouts = self.get_checkouts()
        if len(checkouts) > 2:
            last_checkout = self.get_checkouts()[-2]
            last_date = datetime.strptime(
                last_checkout["date"], '%Y-%m-%dT%H:%M:%SZ')
            "{price,income,paid,name}"
            member_dict = {}
            for m in members:
                transactions: list[Transaction] = self.session.query(Transaction).filter(
                    Transaction.date > last_date, Transaction.member_id == m.id).all()
                temp_dict = {}
                temp_dict["balance"] = m.balance
                temp_dict["name"] = m.alias if m.alias != "" else m.name

                income_transactions = []
                paid_transactions = []

                for t in transactions:
                    t_dict = t.to_dict()
                    if t_dict["amount"] > 0:
                        # income
                        income_transactions.append(
                            [str(t_dict["description"]).replace("&", "\&"), t.date.strftime('%d.%m.%Y'), t_dict["amount"]])
                    else:
                        # paid
                        paid_transactions.append(
                            [str(t_dict["description"]).replace("&", "\&"), t.date.strftime('%d.%m.%Y'), float(t_dict["amount"])*-1])
                temp_dict["income"] = income_transactions
                temp_dict["paid"] = paid_transactions

                member_dict[m.name] = temp_dict

            return member_dict
        else:
            return {}

    def checkPassword(self, name, password):
        member: Member = self.session.query(
            Member).filter_by(name=name.lower()).first()
        if member is None:
            return None

        hashed_pw = TokenManager.hashPassword(password, member.salt)

        if hashed_pw == member.password:
            return member.id
        else:
            return None

    def check_user(self, name):
        member: Member = self.session.query(
            Member).filter_by(name=name.lower()).first()

        return member

    def get_most_bought_drink_name(self, member_id: int, timestamp: datetime):
        # Round timestamp down to nearest 15-minute interval
        now = datetime.now()
        timestamp = timestamp.replace(second=0, microsecond=0)
        timestamp = timestamp.replace(
            year=now.year, month=now.month, day=now.day)
        timestamp = timestamp - timedelta(minutes=timestamp.minute % 15)

        # Calculate the date 120 days ago
        date_range = timestamp - timedelta(days=120)

        # Check for transactions for given member_id for the rest of the day
        max_drink_amounts = {}
        for i in range(0, 24*4):
            interval_start = timestamp + timedelta(minutes=i*15)
            interval_end = timestamp + timedelta(minutes=(i+1)*15)

            # Get all transactions for member_id within current 15-minute interval
            transactions = self.session.query(Transaction).filter(
                Transaction.member_id == member_id,
                extract('hour', Transaction.date) == interval_start.hour,
                extract('minute', Transaction.date) >= interval_start.minute,
                extract('minute', Transaction.date) < interval_end.minute,
                Transaction.date >= date_range
            ).all()

            transactions = [
                t for t in transactions if "Transfer" not in t.description]

            # Calculate total amount of each drink purchased
            drink_amounts = {}
            for transaction in transactions:
                drink_name = transaction.description
                if drink_name in drink_amounts:
                    drink_amounts[drink_name] += 1
                else:
                    drink_amounts[drink_name] = 1

            # If transactions found in interval, return drink with highest total amount
            if drink_amounts:
                return {"drink": max(drink_amounts, key=drink_amounts.get), "confident": True if i == 0 else False}

            # Update max_drink_amounts with current interval's drink amounts
            for drink, amount in drink_amounts.items():
                if drink in max_drink_amounts:
                    max_drink_amounts[drink] += amount
                else:
                    max_drink_amounts[drink] = amount

        # If no transactions found for any interval, return drink with highest total amount for entire day
        if max_drink_amounts:
            return {"drink": max(max_drink_amounts, key=max_drink_amounts.get), "confident": False}
        else:
            return {"drink": None, "confident": False}

    def get_drink_id_from_name(self, name):
        if name is None:
            print("No favorite drink found")
            return None
        drink: Drink = self.session.query(Drink).filter_by(name=name).first()
        if drink is not None:
            return drink.id
        else:
            # Find closest match using sequence matcher
            drinks = self.session.query(Drink).all()
            best_match = None
            best_match_ratio = 0
            for drink in drinks:
                ratio = SequenceMatcher(None, name, drink.name).ratio()
                if ratio > best_match_ratio:
                    best_match = drink
                    best_match_ratio = ratio

            if best_match_ratio > 0.9:
                return best_match.id
            else:
                return None

    def get_most_bought_drink_id(self, member_id: int, timestamp: datetime) -> int:
        drink_name = self.get_most_bought_drink_name(member_id, timestamp)
        return {"drinkID": self.get_drink_id_from_name(drink_name["drink"]), "confidence": drink_name["confident"]}

    def change_member_password(self, password, member_id):
        member: Member = self.session.query(
            Member).filter_by(id=member_id).first()
        hashedPassword, salt = TokenManager.hashPassword(password)
        member.password = hashedPassword
        member.salt = salt
        self.session.commit()

    def change_user_alias(self, member_id, alias):
        member: Member = self.session.query(
            Member).filter_by(id=member_id).first()
        member.alias = alias
        self.session.commit()

    def change_user_name(self, member_id, name):
        member: Member = self.session.query(
            Member).filter_by(id=member_id).first()

        member.name = name
        self.session.commit()

    def backup_database(self):
        checkouts: list[Checkout] = self.session.query(Checkout).all()
        drinks: list[Drink] = self.session.query(Drink).all()
        favorites: list[Favorite] = self.session.query(Favorite).all()
        members: list[Member] = self.session.query(Member).all()
        transactions: list[Transaction] = self.session.query(Transaction).all()

        output = {}

        output['checkouts'] = []
        output['drinks'] = []
        output['favorites'] = []
        output['members'] = []
        output['transactions'] = []

        for c in checkouts:
            output["checkouts"].append(c.dict())
        for d in drinks:
            output["drinks"].append(d.to_dict())
        for f in favorites:
            output["favorites"].append(f.to_dict())
        for m in members:
            output["members"].append(m.to_dict_with_password())
        for t in transactions:
            output["transactions"].append(t.to_dict_backup())

        success = False
        path = f"{util.tempfile_path}/{util.backup_file_name}"
        os.makedirs(os.path.dirname(
            path), exist_ok=True)
        with open(path, 'w') as f:
            f.write(json.dumps(output))
            success = True

        return success

    def set_current_release(self, release_data):
        release_tag = self.session.query(
            KeyValue).filter_by(key="release_tag").first()
        release_message = self.session.query(
            KeyValue).filter_by(key="release_message").first()
        open_issues = self.session.query(
            KeyValue).filter_by(key="open_issues").first()

        if release_tag == None:
            self.session.add(KeyValue(key="release_tag",
                             value=release_data['release_tag']))
        else:
            release_tag.value = release_data['release_tag']

        if release_message == None:
            self.session.add(KeyValue(key="release_message",
                             value=release_data['release_message']))
        else:
            release_message.value = release_data['release_message']

        if open_issues == None:
            self.session.add(KeyValue(key="open_issues",
                             value=release_data['open_issues']))
        else:
            open_issues.value = release_data['open_issues']

        self.session.commit()

    def get_repo_information(self):
        release_tag = self.session.query(
            KeyValue).filter_by(key="release_tag").first()
        if release_tag is not None:
            release_tag = release_tag.value
        release_message = self.session.query(
            KeyValue).filter_by(key="release_message").first()
        if release_message is not None:
            release_message = release_message.value
        open_issues = self.session.query(
            KeyValue).filter_by(key="open_issues").first()
        if open_issues is not None:
            open_issues = int(open_issues.value)

        return {"releaseTag": release_tag, "releaseMessage": release_message, "openIssues": open_issues}

    def transfer(self, member_id_from, member_id_to, amount, message=None):
        member_from: Member = self.session.query(
            Member).filter_by(id=member_id_from).first()
        member_to: Member = self.session.query(
            Member).filter_by(id=member_id_to).first()

        member_from.balance -= amount
        member_to.balance += amount

        string_to = f"Transfer money to {member_to.alias if member_to.alias!='' else member_to.name}"
        string_from = f"Transfer money from {member_from.alias if member_from.alias!='' else member_from.name}"

        if message is not None:
            string_to = f"Transfer money to {member_to.alias if member_to.alias!='' else member_to.name}: {message}"
            string_from = f"Transfer money from {member_from.alias if member_from.alias!='' else member_from.name}: {message}"

        transaction_minus = Transaction(
            description=string_to,
            member_id=member_from.id,
            amount=-amount,
            date=datetime.now())

        transaction_plus = Transaction(
            description=string_from,
            member_id=member_to.id,
            amount=amount,
            date=datetime.now())

        self.session.add(transaction_minus)
        self.session.add(transaction_plus)

        self.session.commit()

        transaction_minus.connected_transaction_id = transaction_plus.id
        transaction_plus.connected_transaction_id = transaction_minus.id

        self.session.commit()

        return member_from.name

    def add_message(self, member_id, message, from_name=None, emoji=None, request=None):
        self.session.add(Reminder(member_id=member_id, text=message,
                         member_name_from=from_name, emoji=emoji, request=request))
        self.session.commit()

    def get_messages(self, member_id):
        reminders: list[Reminder] = self.session.query(
            Reminder).filter_by(member_id=member_id).all()
        output = []
        for r in reminders:
            output.append(r.to_dict())

        return output

    def remove_messages(self, member_id):
        reminders: list[Reminder] = self.session.query(
            Reminder).filter_by(member_id=member_id).all()

        for r in reminders:
            self.session.delete(r)

        self.session.commit()

    def remove_message(self, message_id):
        reminder: Reminder = self.session.query(
            Reminder).filter_by(id=message_id).first()

        self.session.delete(reminder)

        self.session.commit()

    def get_username_alias(self, member_id):
        member: Member = self.session.query(
            Member).filter_by(id=member_id).first()
        return member.name, member.alias

    def get_safe_name(self, member_id):
        name, alias = self.get_username_alias(member_id)
        output = alias if alias is not None and alias != "" else name
        return output

    def hide_inactive(self):
        if util.auto_hide_days is None:
            return

        latest_transactions = self.session.query(Transaction.member_id, func.max(Transaction.date).label('latest_date')) \
            .group_by(Transaction.member_id).all()

        # Print the results
        for result in latest_transactions:
            try:
                if result[1] < datetime.now()-timedelta(days=int(util.auto_hide_days)):
                    self.change_user_visibility(result[0], True)
                # else:
                #    self.change_user_visibility(result[0], False)
            except:
                pass

    def convert_usernames_to_lower(self):
        members = self.session.query(Member).all()

        for m in members:
            member: Member = m
            member.name = member.name.lower()

        self.session.commit()

    def add_aliases_if_non_existend(self):
        members = self.session.query(Member).all()

        for m in members:
            member: Member = m
            if member.alias == "":
                # Set the username as alias with first letter after spaces capitalized
                member.alias = " ".join([name.capitalize()
                                         for name in member.name.split(" ")])

        self.session.commit()

    def add_token(self, token, member_id, time):
        session: Session = self.session.query(
            Session).filter_by(member_id=member_id).first()
        if session is None:
            self.session.add(
                Session(token=token, member_id=member_id, time=time))
        else:
            session.token = token
            session.time = time

        self.session.commit()

    def delete_token(self, token):
        session: Session = self.session.query(
            Session).filter_by(token=token).first()

        if session is not None:
            self.session.delete(session)
            self.session.commit()

    def load_tokens(self):
        return self.session.query(Session).all()

    def get_config_state(self):
        admin: Member = self.session.query(Member).filter_by(
            name=util.admin_username).first()
        
        return admin.balance

    def set_config_state(self, state):
        admin: Member = self.session.query(Member).filter_by(
            name=util.admin_username).first()
        admin.balance = state
        self.session.commit()

    def restore_database(self, imported_data):
        checkouts: list[Checkout] = self.session.query(Checkout).all()
        drinks: list[Drink] = self.session.query(Drink).all()
        favorites: list[Favorite] = self.session.query(Favorite).all()
        members: list[Member] = self.session.query(Member).all()
        transactions: list[Transaction] = self.session.query(Transaction).all()
        sessions: list[Session] = self.session.query(Session).all()

        for c in checkouts:
            self.session.delete(c)
        for d in drinks:
            self.session.delete(d)
        for f in favorites:
            self.session.delete(f)
        for m in members:
            self.session.delete(m)
        for t in transactions:
            self.session.delete(t)
        for s in sessions:
            self.session.delete(s)

        print("Deleting old data...")
        self.session.commit()
        print("Done")

        for m in imported_data["members"]:
            self.session.add(Member(
                id=m['id'],
                name=m['name'],
                balance=m['balance'],
                hidden=m['hidden'],
                alias=m['alias'],
                password=bytes.fromhex(m['password']),
                salt=m['salt']))

        print("Added members")

        try:
            for d in imported_data["drinks"]:
                self.session.add(Drink(
                    id=d['id'],
                    name=d['name'],
                    stock=d['stock'],
                    price=d['price'],
                    category=d['category']))

            self.session.commit()
        except:
            self.session.rollback()
            for d in imported_data["drinks"]:
                try:
                    self.session.add(Drink(
                        id=d['id'],
                        name=d['name'],
                        stock=d['stock'],
                        price=d['price'],
                        category=d['category']))

                    self.session.commit()
                except:
                    self.session.rollback()
                    print("Failed to import drink", d['name'])

        print("Added drinks")

        for c in imported_data["checkouts"]:
            self.session.add(
                Checkout(
                    id=c['id'],
                    date=datetime.strptime(c['date'], "%Y-%m-%dT%H:%M:%SZ"),
                    current_cash=c['currentCash']))

        self.session.commit()

        print("Added checkouts")

        try:

            for f in imported_data["favorites"]:
                self.session.add(Favorite(
                    id=f['id'],
                    member_id=f['member_id'],
                    drink_id=f['drink_id']))

            self.session.commit()
        except:
            self.session.rollback()
            for f in imported_data["favorites"]:
                try:
                    self.session.add(Favorite(
                        id=f['id'],
                        member_id=f['member_id'],
                        drink_id=f['drink_id']))

                    self.session.commit()
                except:
                    self.session.rollback()
                    print("Failed to import favorite", f['id'])
        print("Added favorites")

        try:

            for t in imported_data["transactions"]:
                self.session.add(Transaction(
                    id=t['id'],
                    description=t['description'],
                    member_id=t['memberID'],
                    amount=t['amount'],
                    date=datetime.strptime(t['date'], "%Y-%m-%dT%H:%M:%SZ"),
                    checkout_id=t['checkout_id']))

            self.session.commit()
        except:
            self.session.rollback()
            print("Failed to import transactions")
            counter = 0
            failed_counter = 0
            for idx, t in enumerate(imported_data["transactions"]):
                try:
                    self.session.add(Transaction(
                        id=t['id'],
                        description=t['description'],
                        member_id=t['memberID'],
                        amount=t['amount'],
                        date=datetime.strptime(
                            t['date'], "%Y-%m-%dT%H:%M:%SZ"),
                        checkout_id=t['checkout_id']))

                    self.session.commit()
                    counter += 1
                    if counter % 100 == 0:
                        print("Imported transaction", idx, "/",
                              len(imported_data["transactions"]))
                        counter = 0
                except:
                    self.session.rollback()
                    failed_counter += 1
                    print("Failed to import transaction",
                          t['id'], t['description'])
            print("Failed to import", failed_counter, "transactions")

        print("Added transactions")

        # Get table names
        tables = inspect(self.db.engine).get_table_names()

        try:
            # For each table update the auto increment value
            for table in tables:
                query = text(
                    f"SELECT setval('{str(table).lower()}_id_seq', (SELECT MAX(id) from {table}));")
                self.session.execute(query)
        except:
            print("Failed to update auto increment values (expected on sqlite))")

        return

    def create_dummy_data(self) -> None:
        hashedPassword, salt = TokenManager.hashPassword(util.admin_password)
        self.session.add(
            Member(name=util.admin_username, password=hashedPassword, salt=salt))
        hashedPassword, salt = TokenManager.hashPassword(
            util.moderator_password)
        self.session.add(
            Member(name=util.moderator_username, password=hashedPassword, salt=salt))
        self.session.add(
            KeyValue(key="version", value=util.CURRENT_VERSION))
        self.session.commit()

        if util.token is not None and util.old_domain is not None:

            # Import Users
            resp = requests.get(f"https://{util.old_domain}/api/users",
                                headers={"x-auth-token": util.token}, timeout=10)

            for user in resp.json():
                self.add_user(user["name"], user["balance"]/100,
                              util.standard_user_password, hidden=True if user["hidden"] == 1 else False)
                print("User", user["name"], "imported")

            print("-->", len(resp.json()), "users imported")
            print()

            # Import Drinks
            resp = requests.get(f"https://{util.old_domain}/api/beverages",
                                headers={"x-auth-token": util.token}, timeout=10)

            for drink in resp.json():
                self.add_drink(
                    drink["name"], drink["price"]/100, drink["stock"])
                print("Drink", drink["name"], "imported")

            print("-->", len(resp.json()), "drinks imported")
            print()

            # Import Transactions
            users = self.get_users()
            transactions = []
            for user in users:
                resp = requests.get(f"https://{util.old_domain}/api/orders/{user['name']}",
                                    headers={"x-auth-token": util.token}, timeout=10)

                for transaction in resp.json():
                    date = datetime.strptime(
                        transaction["timestamp"], "%Y-%m-%d %H:%M:%S")
                    transactions.append(
                        {
                            "description": transaction["reason"],
                            "member_id": user["id"],
                            "amount": transaction["amount"]/100,
                            "date": date
                        })

                    print("Transaction", transaction["reason"],
                          "for user", user["name"], "loaded")
                print()
            print("Sorting transactions...")
            transactions.sort(key=lambda x: x.get('date'))
            print("Done sorting transactions")
            for t in transactions:
                self.session.add(Transaction(description=t["description"],
                                             member_id=t["member_id"],
                                             amount=t["amount"],
                                             date=t["date"]))

            print("Starting to commit Transactions to database")
            self.session.commit()
            print("Done")
