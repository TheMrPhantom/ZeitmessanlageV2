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

api_bp = flask.Blueprint("api", __name__, url_prefix="/api/")
api = Api(api_bp, doc='/docu/', base_url='/api')
app.register_blueprint(api_bp)
socket=Websocket()

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
            socket.send({"action": "stop_timer","time":post_data["time"]})
        return util.build_response("OK")

@api.route('/<int:organization_id>/current/participant')
class api_currentParticipant(Resource):
    def post(self,organization_id):
        """
        Start or stop a timer
        """

        # send websocket message
        socket.send({"action": "changed_current_participant","participant":request.json})
        return util.build_response("OK")

@api.route('/<int:organization_id>/current/faults')
class api_currentFaults(Resource):
    def post(self,organization_id):
        """
        Start or stop a timer
        """

        # send websocket message
        socket.send({"action": "changed_current_fault","fault":request.json})
        return util.build_response("OK")

@api.route('/<int:organization_id>/current/refusals')
class api_currentRefusal(Resource):
    def post(self,organization_id):
        """
        Start or stop a timer
        """

        # send websocket message
        socket.send({"action": "changed_current_refusal","refusal":request.json})
        return util.build_response("OK")



@api.route('/login/check')
class login_Check(Resource):
    @authenticated
    def get(self):
        """
        Check if your login token is valid
        """
        return util.build_response("OK")


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
            return util.build_response("Login successfull", cookieToken=token, cookieMemberID=member_id)
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