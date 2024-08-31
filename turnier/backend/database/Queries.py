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






    def change_user_password(self, member_id, new_password):
        pw_hash, salt = TokenManager.hashPassword(new_password)
        user: Member = self.session.query(
            Member).filter_by(id=member_id).first()
        user.password = pw_hash
        user.salt = salt
        self.session.commit()


    def add_user(self, name, money, password, alias="", hidden=False):
        pw_hash, salt = TokenManager.hashPassword(password)
        new_member = Member(name=name.lower(), balance=money,
                            password=pw_hash, salt=salt, alias=alias, hidden=hidden)
        self.session.add(new_member)
        self.session.commit()
        return new_member





    def delete_user(self, member_id):
        self.session.delete(self.session.query(
            Member).filter_by(id=member_id).first())
        self.session.commit()




    def check_user(self, name):
        member: Member = self.session.query(
            Member).filter_by(name=name.lower()).first()

        return member

    def change_member_password(self, password, member_id):
        member: Member = self.session.query(
            Member).filter_by(id=member_id).first()
        hashedPassword, salt = TokenManager.hashPassword(password)
        member.password = hashedPassword
        member.salt = salt
        self.session.commit()

    def change_user_name(self, member_id, name):
        member: Member = self.session.query(
            Member).filter_by(id=member_id).first()

        member.name = name
        self.session.commit()


    def get_username_alias(self, member_id):
        member: Member = self.session.query(
            Member).filter_by(id=member_id).first()
        return member.name, member.alias

    def get_safe_name(self, member_id):
        name, alias = self.get_username_alias(member_id)
        output = alias if alias is not None and alias != "" else name
        return output

    def convert_usernames_to_lower(self):
        members = self.session.query(Member).all()

        for m in members:
            member: Member = m
            member.name = member.name.lower()

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

        hashedPassword, salt = TokenManager.hashPassword("test")
        
        new_member = Member(name="HSF",alias="hsf", password=hashedPassword, salt=salt)
        self.session.add(
            new_member
            )
        self.session.commit()
        new_turnament=Tournament(name="Test", 
                                    date="2024-08-31", 
                                    judge="Supertoll", 
                                    member_id=new_member.id,
                                    )
        
        self.session.add(new_turnament)
        self.session.commit()

        # Create runs for each height 0-3 runs 0-8
        for i in range(4):
            for j in range(9):
                self.session.add(RunInformation(run=j, height=i, length=0, speed=0, turnament_id=new_turnament.id))
        self.session.commit()
