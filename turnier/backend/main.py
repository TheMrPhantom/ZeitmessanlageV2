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
    socket=Websocket()
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

@api.route('/<int:organization_id>/<string:date>')
class update_data(Resource):
    def post(self,organization_id,date):
        """
        Start or stop a timer
        """

        t:Tournament=db.session.query(Tournament).filter(Tournament.date == date).first()
        
        participants:List[Participant]=db.session.query(Participant).filter(Participant.turnament_id == t.id).all()
        
        t.name=request.json["name"]
        t.judge=request.json["judge"]
        
        t.date= date

        
        runs_from_request=request.json["runs"]
        for run in runs_from_request:
            
            run_info:RunInformation = db.session.query(RunInformation).filter(RunInformation.run == run["run"] and RunInformation.height == run["height"] and RunInformation.turnament_id==t.id).first()           
            run_info.length=run["length"]
            run_info.speed=run["speed"]

        participants_from_request= request.json["participants"]
        for participant in participants_from_request:
            # Find participant in participants list
            participant_info=None

            for p in participants:
                if p.start_number==participant["startNumber"]:
                    participant_info=p
                    break
            
            if participant_info is None:
                # Create new participant
                participant_info=Participant()
                participant_info.turnament_id=t.id
                participant_info.start_number=participant["startNumber"]
                participant_info.sorting=participant["sorting"]
                participant_info.name=participant["name"]
                participant_info.club=participant["club"]
                participant_info.dog=participant["dog"]
                participant_info.skill_level=participant["skillLevel"]
                participant_info.size=participant["size"]
                db.session.add(participant_info)
                
                # Add result_a
                result_a=Result()
                result_a.time=participant["resultA"]["time"]
                result_a.faults=participant["resultA"]["faults"]
                result_a.refusals=participant["resultA"]["refusals"]
                result_a.run=participant["resultA"]["run"]
                db.session.add(result_a)
                participant_info.result_a=result_a

                # Add result_j
                result_j=Result()
                result_j.time=participant["resultJ"]["time"]
                result_j.faults=participant["resultJ"]["faults"]
                result_j.refusals=participant["resultJ"]["refusals"]
                result_j.run=participant["resultJ"]["run"]
                db.session.add(result_j)
                participant_info.result_j=result_j
            else:
                participant_info.start_number=participant["startNumber"]
                participant_info.sorting=participant["sorting"]
                participant_info.name=participant["name"]
                participant_info.club=participant["club"]
                participant_info.dog=participant["dog"]
                participant_info.skill_level=participant["skillLevel"]
                participant_info.size=participant["size"]

                participant_info.result_a.time=participant["resultA"]["time"]
                participant_info.result_a.faults=participant["resultA"]["faults"]
                participant_info.result_a.refusals=participant["resultA"]["refusals"]
                participant_info.result_a.run=participant["resultA"]["run"]

                participant_info.result_j.time=participant["resultJ"]["time"]
                participant_info.result_j.faults=participant["resultJ"]["faults"]
                participant_info.result_j.refusals=participant["resultJ"]["refusals"]
                participant_info.result_j.run=participant["resultJ"]["run"]
        
        db.session.commit()

        socket.send({"action": "reload"})
        return util.build_response("OK")

@api.route('/organization/<string:name>')
class get_organizer_info(Resource):
    def get(self,name):
        """
        Gets the alias of an organization
        """
        
        org:Member=db.session.query(Member).filter(Member.name == name).first()
        
        return util.build_response({"name":org.alias})

@api.route('/<int:organization_id>/<string:secret>/<date>')
class get_data(Resource):
    def get(self,organization_id,secret,date):
        """
        Start or stop a timer
        """
        t:Tournament=db.session.query(Tournament).filter(Tournament.date == date).first()
        
        participants:List[Participant]=db.session.query(Participant).filter(Participant.turnament_id == t.id).all()
        participants=[participant.to_dict() for participant in participants]

        runs:List[RunInformation]=db.session.query(RunInformation).filter(RunInformation.turnament_id == t.id).all()
        runs=[run.to_dict() for run in runs]
        return util.build_response({"name":t.name,
                                    "judge":t.judge,
                                    "date":t.date,
                                    "participants":participants,
                                    "runs":runs})


@api.route('/<int:organization_id>/timer')
class api_timer(Resource):
    def post(self,organization_id):
        """
        Start or stop a timer
        """

        post_data = request.json
        action = post_data["action"]
        if action == "start":
            # send websocket message
            socket.send({"action": "start_timer"})
        if action == "stop":
            # send websocket message
            socket.send({"action": "stop_timer","message":post_data["time"]})
        return util.build_response("OK")

@api.route('/<int:organization_id>/current/participant')
class api_currentParticipant(Resource):
    def post(self,organization_id):
        """
        Start or stop a timer
        """

        # send websocket message
        socket.send({"action": "changed_current_participant","message":request.json})
        return util.build_response("OK")

@api.route('/<int:organization_id>/current/faults')
class api_currentFaults(Resource):
    def post(self,organization_id):
        """
        Start or stop a timer
        """

        # send websocket message
        socket.send({"action": "changed_current_fault","message":request.json})
        return util.build_response("OK")

@api.route('/<int:organization_id>/current/refusals')
class api_currentRefusal(Resource):
    def post(self,organization_id):
        """
        Start or stop a timer
        """

        # send websocket message
        socket.send({"action": "changed_current_refusal","message":request.json})
        return util.build_response("OK")



@api.route('/login/check')
class login_Check(Resource):
    @authenticated
    def get(self):
        """
        Check if your login token is valid
        """
        memberID=request.cookies.get(f"{util.auth_cookie_memberID}memberID")
        member=db.get_user(memberID)

        return util.build_response({"name":member.name, "alias":member.alias,"memberID":member.id})


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

        if member_id is not None:
            util.log("Login", "User logged in")
            token = token_manager.create_token(member_id)

            member=db.get_user(member_id)
            valid_until=member.verified_until.isoformat()
            signed_validation=util.sign_message(valid_until+member.name)

            return util.build_response({"validUntil":valid_until,"signedValidation":signed_validation,"name":member.name,"alias":member.alias}, cookieToken=token, cookieMemberID=member_id)
        
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