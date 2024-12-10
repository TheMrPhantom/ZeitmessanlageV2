import xlrd
from difflib import SequenceMatcher
from datetime import datetime, timedelta
import os
from statistics import mode
import TaskScheduler
from flask import helpers
from flask import request
from flask import send_from_directory
from flask.wrappers import Request
from functools import wraps
import authenticator
import util
from web import *
import json
from database import Queries
from flask_restx import fields, Resource, Api
from flask_restx import reqparse
import flask
import mail
import secrets
from websck import Websocket
from database.Models import *
from typing import List


api_bp = flask.Blueprint("api", __name__, url_prefix="/api/")
api = Api(api_bp, doc='/docu/', base_url='/api')
app.register_blueprint(api_bp)

try:
    socket = Websocket()
except Exception as e:
    print(e)

with app.app_context():
    db = Queries.Queries(sql_database)
    token_manager = authenticator.TokenManager(db)
    db.convert_usernames_to_lower()


def is_admin():
    return int(request.cookies.get(f"{util.auth_cookie_memberID}memberID")) == 1 and token_manager.check_token(request.cookies.get(f"{util.auth_cookie_memberID}memberID"), request.cookies.get(f"{util.auth_cookie_memberID}token"))


def authenticated(fn):
    @wraps(fn)
    def wrapper(*args, **kwargs):
        if not token_manager.check_token(request.cookies.get(f"{util.auth_cookie_memberID}memberID"), request.cookies.get(f"{util.auth_cookie_memberID}token")):
            return util.build_response("Unauthorized", 403)
        return fn(*args, **kwargs)
    wrapper.__name__ = fn.__name__
    return wrapper


def admin(fn):
    @wraps(fn)
    def wrapper(*args, **kwargs):
        if not is_admin():
            return util.build_response("Unauthorized", 403)
        return fn(*args, **kwargs)
    wrapper.__name__ = fn.__name__
    return wrapper


def is_self_or_admin(request, member_id):
    return str(member_id) == str(request.cookies.get(f"{util.auth_cookie_memberID}memberID")) or str(request.cookies.get(f"{util.auth_cookie_memberID}memberID")) == "1"


@api.route('/<string:name>/tournament/<string:date>')
class update_data(Resource):
    def post(self, name, date):
        """
        Update the data of a tournament
        """

        t: Tournament | None = db.session.query(Tournament).filter(
            Tournament.date == date, Member.name == name, Tournament.member_id == Member.id).first()

        if t is None:
            # Create new tournament
            t = Tournament()
            t.name = request.json["name"]
            t.judge = request.json["judge"]
            t.date = date
            t.member_id = db.session.query(Member).filter(
                Member.name == name).first().id
            db.session.add(t)
            db.session.commit()

            # Create runs
            for i in range(4):
                for j in range(9):
                    db.session.add(RunInformation(
                        run=j, height=i, length=0, speed=0, turnament_id=t.id))

            db.session.commit()

        participants: List[Participant] = db.session.query(
            Participant).filter(Participant.turnament_id == t.id).all()

        t.name = request.json["name"]
        t.judge = request.json["judge"]

        t.date = date

        runs_from_request = request.json["runs"]

        for run in runs_from_request:

            run_info: RunInformation = db.session.query(RunInformation).filter(
                RunInformation.run == run["run"], RunInformation.height == run["height"], RunInformation.turnament_id == t.id).first()
            run_info.length = run["length"]
            run_info.speed = run["speed"]

        participants_from_request = request.json["participants"]
        for participant in participants_from_request:
            # Find participant in participants list
            participant_info = None

            for p in participants:
                if p.start_number == participant["startNumber"]:
                    participant_info = p
                    break

            if participant_info is None:
                # Create new participant
                participant_info = Participant()
                participant_info.turnament_id = t.id
                participant_info.start_number = participant["startNumber"]
                participant_info.sorting = participant["sorting"]
                participant_info.name = participant["name"]
                participant_info.club = participant["club"]
                participant_info.dog = participant["dog"]
                participant_info.mail = participant["mail"] if "mail" in participant else ""
                participant_info.association = participant["association"]
                participant_info.association_member_number = participant["associationMemberNumber"]
                participant_info.chip_number = participant["chipNumber"]
                participant_info.measure_dog = participant["measureDog"] if "measureDog" in participant else False
                participant_info.skill_level = participant["skillLevel"]
                participant_info.size = participant["size"]
                participant_info.registered = participant["registered"] if "registered" in participant else False
                participant_info.ready = participant["ready"] if "ready" in participant else False
                participant_info.paid = participant["paid"] if "paid" in participant else False
                db.session.add(participant_info)

                # Add result_a
                result_a = Result()
                result_a.time = participant["resultA"]["time"]
                result_a.faults = participant["resultA"]["faults"]
                result_a.refusals = participant["resultA"]["refusals"]
                result_a.run = participant["resultA"]["run"]
                db.session.add(result_a)
                participant_info.result_a = result_a

                # Add result_j
                result_j = Result()
                result_j.time = participant["resultJ"]["time"]
                result_j.faults = participant["resultJ"]["faults"]
                result_j.refusals = participant["resultJ"]["refusals"]
                result_j.run = participant["resultJ"]["run"]
                db.session.add(result_j)
                participant_info.result_j = result_j
            else:
                participant_info.start_number = participant["startNumber"]
                participant_info.sorting = participant["sorting"]
                participant_info.name = participant["name"]
                participant_info.club = participant["club"]
                participant_info.dog = participant["dog"]
                participant_info.skill_level = participant["skillLevel"]
                participant_info.size = participant["size"]
                participant_info.registered = participant["registered"] if "registered" in participant else False
                participant_info.ready = participant["ready"] if "ready" in participant else False
                participant_info.paid = participant["paid"] if "paid" in participant else False

                participant_info.result_a.time = participant["resultA"]["time"]
                participant_info.result_a.faults = participant["resultA"]["faults"]
                participant_info.result_a.refusals = participant["resultA"]["refusals"]
                participant_info.result_a.run = participant["resultA"]["run"]

                participant_info.result_j.time = participant["resultJ"]["time"]
                participant_info.result_j.faults = participant["resultJ"]["faults"]
                participant_info.result_j.refusals = participant["resultJ"]["refusals"]
                participant_info.result_j.run = participant["resultJ"]["run"]

        db.session.commit()

        # Check what participants to delete
        for p in participants:
            found = False
            for participant in participants_from_request:
                if p.start_number == participant["startNumber"]:
                    found = True
                    break
            if not found:
                db.session.delete(p)
        db.session.commit()

        socket.send(name, {"action": "reload"})
        return util.build_response("OK")


@api.route('/organization/<string:name>')
class get_organizer_info(Resource):
    def get(self, name):
        """
        Gets the alias of an organization
        """

        org: Member = db.session.query(Member).filter(
            Member.name == name).first()

        return util.build_response({"name": org.alias})


@api.route('/<string:name>/tournament/<string:secret>/<date>')
class get_data(Resource):
    def get(self, name, secret, date):
        """
        Get the data of a tournament
        """
        t: Tournament = db.session.query(Tournament).filter(
            Tournament.date == date, Member.name == name, Tournament.member_id == Member.id).first()

        if t is None:
            return util.build_response("Not found", 404)
        elif t.secret != secret:
            return util.build_response("Wrong secret", 403)

        participants: List[Participant] = db.session.query(
            Participant).filter(Participant.turnament_id == t.id).all()
        participants = [participant.to_dict() for participant in participants]

        runs: List[RunInformation] = db.session.query(
            RunInformation).filter(RunInformation.turnament_id == t.id).all()
        runs = [run.to_dict() for run in runs]
        return util.build_response({"name": t.name,
                                    "judge": t.judge,
                                    "date": t.date,
                                    "participants": participants,
                                    "runs": runs})


@api.route('/<string:name>/tournament')
class tournaments_of_club(Resource):
    def put(self, name):
        """
        Create a new tournament
        """

        # Check if date is already used
        db_tournament = db.session.query(Tournament).filter(
            Tournament.date == request.json["date"], Member.name == name, Member.id == Tournament.member_id).first()
        if db_tournament is not None:
            return util.build_response("Date already used", 400)

        post_data = request.json
        t = Tournament()
        t.name = post_data["name"]
        t.judge = post_data["judge"]
        t.date = post_data["date"]
        t.member_id = db.session.query(Member).filter(
            Member.name == name).first().id
        db.session.add(t)

        db.session.commit()

        # Create runs
        for i in range(4):
            for j in range(9):
                db.session.add(RunInformation(run=j, height=i,
                               length=0, speed=0, turnament_id=t.id))

        db.session.commit()

        return util.build_response("OK")

    def delete(self, name):
        """
        Delete a tournament
        """
        post_data = request.json
        t: Tournament = db.session.query(Tournament).filter(
            Tournament.date == post_data["date"], Member.name == name, Tournament.member_id == Member.id).first()

        if t is None:
            return util.build_response("Not found", 404)

        db.session.delete(t)
        db.session.commit()
        return util.build_response("OK")

    def get(self, name):
        """
        Get all tournaments of a club
        """
        tournaments: List[Tournament] = db.session.query(Tournament).filter(
            Member.name == name, Tournament.member_id == Member.id).all()
        tournaments = [t.to_dict() for t in tournaments]
        return util.build_response({"name": name, "turnaments": tournaments})


@api.route('/<string:name>')
class organizer(Resource):
    def get(self, name):
        """
        Get all data of an organizer
        """

        memberID = request.cookies.get(f"{util.auth_cookie_memberID}memberID")

        member = db.get_user(memberID)
        if member.name != name and memberID != "1":
            return util.build_response("Unauthorized", 403)

        tournaments: List[Tournament] = db.session.query(Tournament).filter(
            Member.name == name, Tournament.member_id == Member.id).all()
        tournaments = [t.to_dict() for t in tournaments]
        return util.build_response({"name": name, "turnaments": tournaments})


@api.route('/<string:name>/timer')
class api_timer(Resource):
    def post(self, name):
        """
        Start or stop a timer
        """

        post_data = request.json
        action = post_data["action"]
        if action == "start":
            # send websocket message
            socket.send(name, {"action": "start_timer"})
        if action == "stop":
            # send websocket message
            socket.send(name, {"action": "stop_timer",
                        "message": post_data["time"]})
        return util.build_response("OK")


@api.route('/<string:name>/current/participant')
class api_currentParticipant(Resource):
    def post(self, name):
        """
        Change the current participant
        """

        # send websocket message
        socket.send(
            name, {"action": "changed_current_participant", "message": request.json})
        return util.build_response("OK")


@api.route('/<string:name>/current/faults')
class api_currentFaults(Resource):
    def post(self, name):
        """
        Change current faults
        """

        # send websocket message
        socket.send(
            name, {"action": "changed_current_fault", "message": request.json})
        return util.build_response("OK")


@api.route('/<string:name>/current/refusals')
class api_currentRefusal(Resource):
    def post(self, name):
        """
        Change current refusals
        """

        # send websocket message
        socket.send(
            name, {"action": "changed_current_refusal", "message": request.json})
        return util.build_response("OK")


@api.route('/<string:name>/<string:date>/qr')
class qr_code(Resource):
    def get(self, name, date):
        """
        Get the qr code for a tournament
        """

        t: Tournament = db.session.query(Tournament).filter(
            Tournament.date == date, Member.name == name, Tournament.member_id == Member.id).first()

        return util.build_response(t.secret)


@api.route('/members')
class members(Resource):
    def get(self):
        """
        Get the organizers
        """

        members = db.session.query(Member).all()

        # to dict array
        members = [member.to_dict() for member in members]

        # fitler admin and moderator
        members = [member for member in members if member["id"]
                   != 1 and member["id"] != 2]

        return util.build_response(members)

    def put(self):
        """
        Add a member
        """
        post_data = request.json
        member: Member | None = db.session.query(Member).filter(
            Member.name == post_data["name"]).first()

        if member is not None:
            return util.build_response("Member already exists", 409)

        pw, salt = authenticator.TokenManager.hashPassword(
            post_data["password"])
        member = Member(name=post_data["name"],
                        alias=post_data["alias"],
                        password=pw,
                        salt=salt,
                        verified_until=datetime.fromisoformat(
            post_data["verifiedUntil"]),
            reference=post_data["reference"])
        db.session.add(member)
        db.session.commit()

        members = db.session.query(Member).all()
        # to dict array
        members = [member.to_dict() for member in members]

        # fitler admin and moderator
        members = [member for member in members if member["id"]
                   != 1 and member["id"] != 2]

        return util.build_response(members)

    def delete(self):
        """
        Delete a member
        """
        post_data = request.json
        member: Member | None = db.session.query(Member).filter(
            Member.name == post_data["name"]).first()

        if member is None:
            return util.build_response("Member not found", 404)

        db.session.delete(member)
        db.session.commit()

        members = db.session.query(Member).all()
        # to dict array
        members = [member.to_dict() for member in members]

        # fitler admin and moderator
        members = [member for member in members if member["id"]
                   != 1 and member["id"] != 2]

        return util.build_response(members)


@api.route('/member/<string:attribute>')
class modify_member(Resource):
    def post(self, attribute):
        """
        Change an attribute of a user
        """
        post_data = request.json

        # Get member name
        member_name = post_data["name"]
        member: Member = db.session.query(Member).filter(
            Member.name == member_name).first()

        if member is None:
            return util.build_response("Member not found", 404)

        if attribute == "alias":
            member.alias = post_data["value"]
        elif attribute == "password":
            pw, salt = authenticator.TokenManager.hashPassword(
                post_data["value"])
            member.password = pw
            member.salt = salt
        elif attribute == "verifiedUntil":
            member.verified_until = datetime.fromisoformat(post_data["value"])
        elif attribute == "reference":
            member.reference = post_data["value"]
        else:
            print("asdf")
            return util.build_response("Attribute not found", 404)

        db.session.commit()

        members = db.session.query(Member).all()
        # to dict array
        members = [member.to_dict() for member in members]

        # fitler admin and moderator
        members = [member for member in members if member["id"]
                   != 1 and member["id"] != 2]

        return util.build_response(members)


@api.route('/login/check')
class login_Check(Resource):
    @authenticated
    def get(self):
        """
        Check if your login token is valid
        """
        memberID = request.cookies.get(f"{util.auth_cookie_memberID}memberID")
        member = db.get_user(memberID)

        return util.build_response({"name": member.name, "alias": member.alias, "memberID": member.id})


@api.route('/start-oidc')
class start_oidc(Resource):
    def get(self):
        """
        Start oidc flow
        """

        return util.build_response(f"{util.OIDC_AUTH_PATH}?client_id={util.OIDC_CLIENT_ID}&response_type=code&scope=email%20profile%20openid&state={secrets.token_urlsafe(32)}")


@api.route('/oidc-redirect')
class oidc_redirect(Resource):
    def get(self):
        """
        Handle redirect of auth provider
        """

        # get query parameters
        code = request.args.get('code')
        # state = request.args.get('state')
        # scope = request.args.get('scope')

        token_endpoint = util.OIDC_AUTH_TOKEN
        redirect_uri = util.OIDC_AUTH_REDIRECT

        token = util.get_oidc_token(token_endpoint, code, redirect_uri)
        userinfo = util.get_user_info(access_token=token,
                                      resource_url=util.OIDC_USER_INFO)

        # check if user exists
        user = db.check_user(userinfo["username"])
        login_token = None
        user_id = None

        if user is None:
            # create user
            new_member = db.add_user(userinfo["username"],
                                     0, util.standard_user_password, alias=userinfo["name"], hidden=util.OIDC_USER_NEEDS_VERIFICATION)
            mail.send_welcome_mail(new_member.name)

            if util.OIDC_USER_NEEDS_VERIFICATION:
                return flask.redirect(util.OIDC_REDIRECT_MAIN_PAGE+"/message/new-user", code=302)

            user_id = new_member.id
            login_token = token_manager.create_token(user_id)

        else:
            if user.hidden:
                return flask.redirect(util.OIDC_REDIRECT_MAIN_PAGE+"/message/activate", code=302)

            # log user in
            user_id = user.id
            login_token = token_manager.create_token(user_id)

        r = flask.redirect(util.OIDC_REDIRECT_MAIN_PAGE, code=302)
        r.set_cookie(f"{util.auth_cookie_memberID}memberID", str(user_id),
                     max_age=util.cookie_expire, samesite='Strict', secure=not util.logging_enabled)
        r.set_cookie(f"{util.auth_cookie_memberID}token", login_token,
                     max_age=util.cookie_expire, samesite='Strict', secure=not util.logging_enabled)

        return r


@api.route('/login/admin/check')
class login_Check_Admin(Resource):
    @admin
    def get(self):
        """
        Check if your token is a valid admin token
        """
        return util.build_response("OK")


model = api.model('Login', {
    'name': fields.String(description='Name of the user that wants to log in', required=True),
    'password': fields.String(description='Password of the user that wants to log in', required=True)
})


@api.route('/login')
class login(Resource):
    @api.doc(body=model)
    def post(self):
        """
        Get the memberID and token for using the api

        The <b>memberID</b> and <b>token</b> have to be send with every request as cookies
        """
        post_data = request.json
        name = post_data["name"]
        password = post_data["password"]
        member_id = db.checkPassword(name, password)

        if member_id == 1:
            token = token_manager.create_token(member_id)
            member = db.get_user(member_id)
            valid_until = (datetime.now()+timedelta(days=90)).isoformat()
            signed_validation = util.sign_message(valid_until+member.name)
            return util.build_response({"validUntil": valid_until, "signedValidation": signed_validation, "name": member.name, "alias": member.alias}, cookieToken=token, cookieMemberID=member_id)

        if member_id is not None:
            util.log("Login", "User logged in")
            token = token_manager.create_token(member_id)

            member = db.get_user(member_id)
            valid_until = member.verified_until.isoformat()
            signed_validation = util.sign_message(valid_until+member.name)

            return util.build_response({"validUntil": valid_until, "signedValidation": signed_validation, "name": member.name, "alias": member.alias}, cookieToken=token, cookieMemberID=member_id)

        return util.build_response("Unauthorized", code=403)


@api.route('/cookies')
class cookie(Resource):
    def get(self):
        """
        Get the memberID and token for using the api
        """

        return util.build_response({"memberID": request.cookies.get(f"{util.auth_cookie_memberID}memberID"), "token": request.cookies.get(f"{util.auth_cookie_memberID}token")})


@api.route('/logout')
class logout(Resource):
    @authenticated
    def post(self):
        """
        Invalidates the current token
        """
        token_manager.delete_token(request.cookies.get(
            f"{util.auth_cookie_memberID}token"))
        util.log(
            "Logout", f"MemberID: {request.cookies.get(f'{util.auth_cookie_memberID}memberID')}")
        return util.build_response("OK")


if __name__ == "__main__":
    if util.logging_enabled:
        app.run("0.0.0.0", threaded=True, debug=True)
    else:
        from waitress import serve
        serve(app, host="0.0.0.0", port=5000, threads=4)
