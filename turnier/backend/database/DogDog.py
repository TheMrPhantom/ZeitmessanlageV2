import sqlalchemy as sql
from web import sql_database as db
from sqlalchemy.orm import relationship
import secrets


def gen_secret():
    return secrets.token_urlsafe(16)


class Result(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    time = sql.Column(sql.Float, nullable=False)
    faults = sql.Column(sql.Integer, nullable=False)
    refusals = sql.Column(sql.Integer, nullable=False)
    type = sql.Column(sql.Integer, nullable=False)

    def to_dict(self):
        return {
            "time": self.time,
            "faults": self.faults,
            "refusals": self.refusals,
            "type": self.type
        }


class Participant(db.Model):

    id = sql.Column(sql.Integer, primary_key=True)
    start_number = sql.Column(sql.Integer, nullable=False)
    sorting = sql.Column(sql.Integer, nullable=False)
    name = sql.Column(sql.String, nullable=False)
    club = sql.Column(sql.String, nullable=False)
    dog = sql.Column(sql.String, nullable=False)
    skill_level = sql.Column(sql.Integer, nullable=False)
    size = sql.Column(sql.Integer, nullable=False)
    is_youth = sql.Column(sql.Boolean, nullable=False)
    mail = sql.Column(sql.String, nullable=False)
    association = sql.Column(sql.String, nullable=False)
    association_member_number = sql.Column(sql.String, nullable=False)
    chip_number = sql.Column(sql.String, nullable=False)
    measure_dog = sql.Column(sql.Boolean, default=False, nullable=False)
    registered = sql.Column(sql.Boolean, default=False, nullable=False)
    ready = sql.Column(sql.Boolean, default=False, nullable=False)
    paid = sql.Column(sql.Boolean, nullable=False)
    turnament_id = sql.Column(sql.Integer, sql.ForeignKey(
        'tournament.id', ondelete='CASCADE'), nullable=True)
    result_a_id = sql.Column(sql.Integer, sql.ForeignKey(
        'result.id', ondelete='SET NULL'), nullable=True)
    result_a = relationship('database.DogDog.Result', foreign_keys=[
                            result_a_id], lazy="joined")
    result_j_id = sql.Column(sql.Integer, sql.ForeignKey(
        'result.id', ondelete='SET NULL'), nullable=True)
    result_j = relationship('database.DogDog.Result', foreign_keys=[
                            result_j_id], lazy="joined")

    def to_dict(self):
        return {
            "startNumber": self.start_number,
            "sorting": self.sorting,
            "name": self.name,
            "isYouth": self.is_youth,
            "club": self.club,
            "dog": self.dog,
            "skillLevel": self.skill_level,
            "size": self.size,
            "association": self.association,
            "associationMemberNumber": self.association_member_number,
            "chipNumber": self.chip_number,
            "measureDog": self.measure_dog,
            "registered": self.registered,
            "ready": self.ready,
            "paid": self.paid,
            "resultA": self.result_a.to_dict() if self.result_a else None,
            "resultJ": self.result_j.to_dict() if self.result_j else None
        }

    def to_admin_dict(self):
        return {
            "startNumber": self.start_number,
            "sorting": self.sorting,
            "name": self.name,
            "isYouth": self.is_youth,
            "club": self.club,
            "dog": self.dog,
            "skillLevel": self.skill_level,
            "size": self.size,
            "mail": self.mail,
            "association": self.association,
            "associationMemberNumber": self.association_member_number,
            "chipNumber": self.chip_number,
            "measureDog": self.measure_dog,
            "registered": self.registered,
            "ready": self.ready,
            "paid": self.paid,
            "resultA": self.result_a.to_dict() if self.result_a else None,
            "resultJ": self.result_j.to_dict() if self.result_j else None
        }


class RunInformation(db.Model):
    __tablename__ = 'RunInformation'
    id = sql.Column(sql.Integer, primary_key=True)
    run = sql.Column(sql.Integer, nullable=False)
    height = sql.Column(sql.Integer, nullable=False)
    length = sql.Column(sql.Integer, nullable=False)
    speed = sql.Column(sql.Float, nullable=False)
    isGame = sql.Column(sql.Boolean, nullable=False)
    turnament_id = sql.Column(sql.Integer, sql.ForeignKey(
        'tournament.id', ondelete='CASCADE'), nullable=True)

    def to_dict(self):
        return {
            "run": self.run,
            "height": self.height,
            "length": self.length,
            "speed": self.speed,
            "isGame": self.isGame
        }


class Tournament(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    member_id = sql.Column(sql.Integer, sql.ForeignKey(
        'member.id', ondelete='CASCADE'), nullable=False)
    date = sql.Column(sql.String, nullable=False)
    judge = sql.Column(sql.String, nullable=False)
    name = sql.Column(sql.String, nullable=False)
    secret = sql.Column(sql.String, nullable=False, default=gen_secret)
    participants = relationship('database.DogDog.Participant', lazy="joined")
    runs = relationship('database.DogDog.RunInformation', lazy="joined")

    def to_dict(self):
        return {
            "date": self.date,
            "judge": self.judge,
            "name": self.name,
            "participants": [p.to_dict() for p in self.participants],
            "runs": [r.to_dict() for r in self.runs]
        }

    def to_admin_dict(self):
        return {
            "date": self.date,
            "judge": self.judge,
            "name": self.name,
            "participants": [p.to_admin_dict() for p in self.participants],
            "runs": [r.to_dict() for r in self.runs]
        }
