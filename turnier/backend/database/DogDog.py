import sqlalchemy as sql
from web import sql_database as db
from sqlalchemy.orm import relationship



class Result(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    time = sql.Column(sql.Float, nullable=False)
    faults = sql.Column(sql.Integer, nullable=False)
    refusals = sql.Column(sql.Integer, nullable=False)
    run = sql.Column(sql.Integer, nullable=False)

    def to_dict(self):
        return {
            "time": self.time,
            "faults": self.faults,
            "refusals": self.refusals,
            "run": self.run
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
    turnament_id = sql.Column(sql.Integer, sql.ForeignKey('tournament.id', ondelete='SET NULL'), nullable=True)
    result_a_id = sql.Column(sql.Integer, sql.ForeignKey('result.id', ondelete='SET NULL'), nullable=True)
    result_a = relationship('database.DogDog.Result',foreign_keys=[result_a_id], lazy="joined")
    result_j_id = sql.Column(sql.Integer, sql.ForeignKey('result.id', ondelete='SET NULL'), nullable=True)
    result_j = relationship('database.DogDog.Result', foreign_keys=[result_j_id], lazy="joined")

    def to_dict(self):
        return {
            "startNumber": self.start_number,
            "sorting": self.sorting,
            "name": self.name,
            "club": self.club,
            "dog": self.dog,
            "skillLevel": self.skill_level,
            "size": self.size,
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
    turnament_id = sql.Column(sql.Integer, sql.ForeignKey('tournament.id', ondelete='SET NULL'), nullable=True)

    def to_dict(self):
        return {
            "run": self.run,
            "height": self.height,
            "length": self.length,
            "speed": self.speed
        }
    
class Tournament(db.Model):
    id = sql.Column(sql.Integer, primary_key=True)
    member_id = sql.Column(sql.Integer, sql.ForeignKey('member.id', ondelete='SET NULL'), nullable=False)
    date = sql.Column(sql.String, nullable=False, unique=True)
    judge = sql.Column(sql.String, nullable=False)
    name = sql.Column(sql.String, nullable=False)
    participants = relationship('database.DogDog.Participant', lazy="joined")
    runs = relationship('database.DogDog.RunInformation', lazy="joined")