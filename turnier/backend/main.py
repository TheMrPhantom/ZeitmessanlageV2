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
from database.Drink import Drink
import util
from web import *
import json
from database import Queries
from flask_restx import fields, Resource, Api
from flask_restx import reqparse
import flask
import mail
import secrets

api_bp = flask.Blueprint("api", __name__, url_prefix="/api/")
api = Api(api_bp, doc='/docu/', base_url='/api')
app.register_blueprint(api_bp)


with app.app_context():
    db = Queries.Queries(sql_database)
    token_manager = authenticator.TokenManager(db)

    db.hide_inactive()
    db.convert_usernames_to_lower()
    db.add_aliases_if_non_existend()

    taskScheduler = TaskScheduler.TaskScheduler()
    taskScheduler.add_Daily_Task(db.hide_inactive)
    taskScheduler.start()


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


model_amount = api.model('Amount', {
    'amount': fields.Float(required=True)
})


@api.route('/users')
class GET_USERS(Resource):
    @authenticated
    def get(self):
        """
        Gets all users
        """
        return util.build_response(db.get_users())


@api.route('/users/<int:member_id>/favorites')
class get_user_favorites(Resource):
    @authenticated
    def get(self, member_id):
        """
        Get favorite drinks of a user
        """
        return util.build_response(db.get_user_favorites(member_id))


@api.route('/users/<int:member_id>/favorites/generated')
class get_user_favorites_generated(Resource):
    @authenticated
    def get(self, member_id):
        """
        Get favorite drinks of a user
        """
        return util.build_response(db.get_most_bought_drink_id(member_id, datetime.now()))


@api.route('/users/<int:member_id>/history')
class get_user_history(Resource):
    @authenticated
    def get(self, member_id):
        """
        Get history of a user
        """
        return util.build_response(db.get_user_history(member_id))


@api.route('/users/<int:member_id>/favorites/add/<int:drink_id>')
class add_user_favorite(Resource):
    @authenticated
    def put(self, member_id, drink_id):
        """
        Add favorite drink of a user
        """
        db.add_user_favorite(member_id, drink_id)
        return util.build_response("Added favorite")


@api.route('/users/<int:member_id>/favorites/remove/<int:drink_id>')
class get_user_favorites(Resource):
    @authenticated
    def delete(self, member_id, drink_id):
        """
        Remove favorite drink of a user
        """
        db.remove_user_favorite(member_id, drink_id)
        return util.build_response("Removed favorite")


model_password = api.model('Password', {
    'password': fields.String(required=True)
})


@api.route('/users/<int:member_id>/password')
class change_user_password(Resource):
    @admin
    @api.doc(body=model_password)
    def post(self, member_id):
        """
        Change the password of a user
        """
        db.change_user_password(member_id, request.json["password"])
        return util.build_response("Password changed")


@api.route('/users/<int:member_id_from>/transfer/<int:member_id_to>')
class transfer_money(Resource):
    @authenticated
    @api.doc(body=model_amount)
    def post(self, member_id_from, member_id_to):
        """
        Transfer money from one member to another
        """

        amount = float(request.json['amount'])

        if amount < 0:
            return util.build_response("Amount has to be positive", code=412)

        if is_self_or_admin(request, member_id_from):
            message = None
            if 'reason' in request.json:
                message = request.json['reason']

            from_name = db.transfer(
                member_id_from, member_id_to, amount, message)

            from_name, from_alias = db.get_username_alias(member_id_from)
            to_name, to_alias = db.get_username_alias(member_id_to)

            db.add_message(
                member_id_to,
                f"Du hast {'{:.2f}'.format(amount)}€ von {from_name} erhalten{f' mit folgender Nachricht: {message}' if message is not None else ''}",
                from_name,
                request.json['emoji'] if 'emoji' in request.json else None
            )

            from_message = f" mit folgender Nachricht: '{message}' " if message is not None else ' '
            to_message = f" mit folgender Nachricht: '{message}' " if message is not None else ' '

            mail.send_transfer_mails(from_name,
                                     from_alias,
                                     to_name,
                                     to_alias,
                                     f"Du hast {'{:.2f}'.format(amount)}€ an {to_alias if to_alias !='' else to_name}{to_message}gesendet",
                                     f"Du hast {'{:.2f}'.format(amount)}€ von {from_alias if from_alias !='' else from_name}{from_message}erhalten")

        else:
            return util.build_response("Your are not allowed to transfer money from this account", code=403)

        return util.build_response(f"{amount}€ transfered")


model_requestTransfer = api.model('Transfer request', {
    'fromUser': fields.List(fields.Integer),
    'toUser': fields.Integer(required=True),
    'amount': fields.Float(description="Amount per user"),
    'description': fields.String()
})


@api.route('/users/request/transfer')
class request_transfer(Resource):
    @authenticated
    @api.doc(body=model_requestTransfer)
    def post(self):
        """
        Request money from users
        """

        fromUser = request.json['fromUser']
        toUser = request.json['toUser']
        amount = request.json['amount']
        description = request.json['description']

        safeNameTo = db.get_safe_name(toUser)

        outputDescription = f"Es wurden {amount:.2f}€ von dir angefordert" if description is None else f"Es wurden {amount:.2f}€ von dir angefordert, wegen {description}"

        for user in fromUser:
            username, alias = db.get_username_alias(user)
            safeNameFrom = db.get_safe_name(user)
            mail_body = util.money_request_mail_test.format(
                name=safeNameFrom, requester=safeNameTo, money=f"{amount:.2f}", url=util.domain)
            mail.send_mail("Überweisungsanfrage",
                           mail.mail_from_username(username), mail_body)
            db.add_message(user, outputDescription, f"von {safeNameTo}", request=json.dumps({
                           "to": toUser, "amount": amount}))

        return util.build_response("Ok")


@api.route('/users/<int:member_id>/visibility/toggle')
class toggle_user_visibility(Resource):
    @admin
    def post(self, member_id):
        """
        Toggle the visibility for the dashboard of a user
        """
        db.change_user_visibility(member_id)
        return util.build_response("Changed visibility")


@api.route('/users/<int:member_id>/deposit')
class user_deposit(Resource):
    @admin
    @api.doc(body=model_amount)
    def post(self, member_id):
        """
        Deposit money for a user
        """
        db.deposit_user(member_id, float(request.json["amount"]))
        return util.build_response("Money added")


model = api.model('Alias', {
    'alias': fields.String(required=True)
})


@api.route('/users/<int:member_id>/alias')
class user_deposit(Resource):
    @admin
    @api.doc(body=model)
    def post(self, member_id):
        """
        Set the alias of a user
        """
        db.change_user_alias(member_id, str(request.json["alias"]))
        return util.build_response("Alias changed")


model = api.model('Name', {
    'name': fields.String(required=True)
})


@api.route('/users/<int:member_id>/name')
class user_deposit(Resource):
    @admin
    @api.doc(body=model)
    def post(self, member_id):
        """
        Set the username of a user
        """
        if str(request.json["name"]) == "":
            return util.build_response("Name cannot be empty", code=412)
        db.change_user_name(member_id, str(request.json["name"]))
        return util.build_response("Name changed")


@api.route('/users/<int:member_id>/delete')
class delete_user(Resource):
    @admin
    def delete(self, member_id):
        """
        Delete a user
        """
        if member_id > 2:
            db.delete_user(member_id)
            return util.build_response("User deleted")
        else:
            return util.build_response("Admin and moderator cannot be deleted", code=412)


@api.route('/users/<int:member_id>/messages')
class user_messages(Resource):
    @authenticated
    def get(self, member_id):
        """
        Returns all messages of a user
        """
        if is_self_or_admin(request, member_id):
            return util.build_response(db.get_messages(member_id))
        return util.build_response("Unauthorized", code=403)

    @authenticated
    def delete(self, member_id):
        """
        Removes all messages of a user
        """
        if is_self_or_admin(request, member_id):
            return util.build_response(db.remove_messages(member_id))
        return util.build_response("Unauthorized", code=403)


@api.route('/users/<int:member_id>/messages/<int:message_id>')
class user_messages(Resource):
    @authenticated
    def delete(self, member_id, message_id):
        """
        Removes all messages of a user
        """
        if is_self_or_admin(request, member_id):
            return util.build_response(db.remove_message(message_id))
        return util.build_response("Unauthorized", code=403)


model = api.model('Add User', {
    'name': fields.String(description='Name of the new user', required=True),
    'alias': fields.String(description='Alias of the user', required=False),
    'money': fields.Float(description='Initial balance of the user', required=True),
    'password': fields.String(description='Initial password of user', required=True),
})


@api.route('/users/add')
class add_user(Resource):
    @admin
    @api.doc(body=model)
    def post(self):
        """
        Add a user
        """
        if 'alias' in request.json:
            db.add_user(request.json["name"],
                        request.json["money"], request.json["password"], alias=request.json["alias"])
        else:
            db.add_user(request.json["name"],
                        request.json["money"], request.json["password"])

        mail.send_welcome_mail(request.json["name"])

        return util.build_response("User added")


@api.route('/drinks')
class get_drinks(Resource):
    @authenticated
    def get(self):
        """
        Get all drinks
        """
        return util.build_response(db.get_drinks())


@api.route('/drinks/search/<string:drink_search>')
class search_drinks(Resource):
    @authenticated
    def get(self, drink_search):
        """
        Search for drinks
        """
        drinks: list[Drink] = db.get_drinks()
        prepared_search = str(drink_search).replace(" ", "").lower()

        for d in drinks:
            if str(d["name"]).replace(" ", "").lower() == prepared_search:
                return util.build_response({"precise": True, "drinks": d})

        for d in drinks:
            d['matching'] = SequenceMatcher(
                None, d["name"], drink_search).ratio()

        drinks.sort(key=lambda x: x['matching'])
        drinks.reverse()

        return util.build_response({"precise": False, "drinks": drinks})


@api.route('/drinks/categories')
class get_user_categories(Resource):
    @authenticated
    def get(self):
        """
        Get all categories
        """
        return util.build_response(db.get_drink_categories())


@api.route('/drinks/<int:drink_id>/price')
class set_drink_price(Resource):
    @admin
    @api.doc(body=model_amount)
    def post(self, drink_id):
        """
        Set the price of a drink
        """
        db.change_drink_price(drink_id, request.json["amount"])
        return util.build_response("Price changed")


@api.route('/drinks/<int:drink_id>/stock')
class set_drink_stock(Resource):
    @admin
    @api.doc(body=model_amount)
    def post(self, drink_id):
        """
        Set the current stock of a drink
        """
        db.change_drink_stock(drink_id, request.json["amount"])
        return util.build_response("Stock changed")


model = api.model('Drink-Name', {
    'name': fields.String(description='Name of the drink', required=True),
})


@api.route('/drinks/<int:drink_id>/name')
class set_drink_name(Resource):
    @admin
    @api.doc(body=model)
    def post(self, drink_id):
        """
        Set the name of a drink
        """
        if request.json["name"] == "":
            return util.build_response("Can not be empty", code=406)
        db.change_drink_name(drink_id, request.json["name"])
        return util.build_response("Name changed")


model = api.model('Drink-Category', {
    'category': fields.String(description='Category of the drink', required=True),
})


@api.route('/drinks/<int:drink_id>/category')
class set_drink_category(Resource):
    @admin
    @api.doc(body=model)
    def post(self, drink_id):
        """
        Set the category of a drink
        """
        db.change_drink_category(
            drink_id, request.json["category"] if request.json["category"] != "" else util.default_drink_category)
        return util.build_response("Category changed")


@api.route('/drinks/<int:drink_id>/stock/increase')
class set_drink_stock_increase(Resource):
    @admin
    @api.doc(body=model_amount)
    def post(self, drink_id):
        """
        Increase the current stock of a drink
        """
        db.change_drink_stock(
            drink_id, request.json["amount"], is_increase=True)
        return util.build_response("Stock changed")


@api.route('/drinks/<int:drink_id>/delete')
class delete_drink(Resource):
    @admin
    def delete(self, drink_id):
        """
        Delete a drink
        """
        db.delete_drink(drink_id)
        return util.build_response("Drink deleted")


model = api.model('Add-Drink', {
    'name': fields.String(description='What the drink should be named', required=True),
    'price': fields.Float(description='The price in euro', required=True),
    'stock': fields.Integer(description='The current stock', required=True),
    'category': fields.String(description='The category that the drink should be sorted into', required=False),
})


@api.route('/drinks/add')
class add_drink(Resource):
    @admin
    @api.doc(body=model)
    def put(self):
        """
        Add a drink
        """
        db.add_drink(request.json["name"],
                     request.json["price"], request.json["stock"], request.json["category"] if "category" in request.json else None)
        return util.build_response("Drink added")


buy_drink_model = api.model('Drink Buy', {
    'drinkID': fields.Integer(description='ID of the Drink that will be bought', required=True),
    'memberID': fields.Integer(description='ID of the user that buy the drink', required=True)
})


@api.route('/drinks/buy')
class buy_drink(Resource):
    @authenticated
    @api.doc(body=buy_drink_model)
    def post(self):
        """
        Buy a drink
        """
        status = db.buy_drink(
            request.json["memberID"], request.json["drinkID"])
        if "name" in status:
            return util.build_response(f"Drink {status['name']} bought")
        else:
            return util.build_response("Drink does not exist", code=400)


@api.route('/transactions')
class get_transactions(Resource):
    @authenticated
    def get(self):
        """
        Get all transactions
        """
        return util.build_response(db.get_transactions())


@api.route('/transactions/limit/<int:limit>')
class get_transactions_limited(Resource):
    @authenticated
    def get(self, limit):
        """
        Get the lastest x transactions
        """
        return util.build_response(db.get_transactions(limit))


@api.route('/transactions/<int:transaction_id>/undo')
class undo_transaction(Resource):
    @authenticated
    def post(self, transaction_id):
        """
        Undo the given transaction
        """
        if not is_admin():
            transaction = db.get_transaction(transaction_id)
            transaction_date = datetime.strptime(
                transaction['date'], "%Y-%m-%dT%H:%M:%SZ")
            if transaction_date+timedelta(minutes=util.undo_timelimit) < datetime.now():
                return util.build_response("TooLate", code=412)

            if "Transfer money" in transaction["description"]:
                if not is_self_or_admin(request, transaction['memberID']):
                    return util.build_response("NotYourTransaction", code=412)

        db.delete_transaction(transaction_id)
        return util.build_response("Transaction undone")


member_model = api.model('Member-Checkout', {
    'memberID': fields.Integer,
    'amount': fields.Float
})

invoice_model = api.model('Invoice-Checkout', {
    'name': fields.Integer,
    'amount': fields.Float
})

model = api.model('Do-Checkout', {
    'newCash': fields.Float(description='The value that what was counted after the checkout', required=False),
    'members': fields.Nested(member_model, as_list=True),
    'invoices': fields.Nested(invoice_model, as_list=True)
})


@api.route('/checkout')
class do_checkout(Resource):
    @admin
    @api.doc(body=model)
    def put(self):
        """
        Create a checkout
        """
        db.do_checkout(request.json)
        mail_infos = db.get_checkout_mail()
        if util.mail_server is not None:
            mail.send_checkout_mails(mail_infos)
        return util.build_response("")

    @admin
    def get(self):
        """
        Get all checkouts
        """
        return util.build_response(db.get_checkouts())


@api.route('/checkout/<int:checkout_id>')
class get_checkout_expanded(Resource):
    @admin
    def get(self, checkout_id):
        """
        Get the extended infos of a checkout
        """
        return util.build_response(db.get_checkout_expanded(checkout_id))


@api.route('/settings/backup')
class get_backup(Resource):
    @admin
    def get(self):
        """
        Get a json backup of the database
        """
        if db.backup_database():
            return send_from_directory(util.tempfile_path, util.backup_file_name)
        return util.build_response("Error creating file", code=500)


@api.route('/settings/restore')
class restore_db(Resource):
    @admin
    def post(self):
        """
        Restore from a json backup
        """
        file = request.files['file']
        file_json = json.loads(file.stream.read().decode("utf-8"))
        db.restore_database(file_json)
        return util.build_response("ok")


@api.route('/settings/password/admin')
class change_admin_password(Resource):
    @admin
    @api.doc(body=model_password)
    def post(self):
        """
        Change the admin password
        """
        db.change_member_password(request.json['password'], 1)
        return util.build_response("ok")


@api.route('/settings/password/kiosk')
class change_kiosk_password(Resource):
    @admin
    @api.doc(body=model_password)
    def post(self):
        """
        change the kiosk user password
        """
        db.change_member_password(request.json['password'], 2)
        return util.build_response("ok")


config_model = api.model('Config state', {
    'state': fields.Integer
})


@api.route('/config/status')
class check_config_state(Resource):
    @api.doc()
    def get(self):
        """
        get the config state of the drinklist
        """
        return util.build_response(db.get_config_state())

    @api.doc(body=config_model)
    def post(self):
        """
        change the config state of the drinklist
        """
        if db.get_config_state() != 0 and not is_admin():
            return util.build_response("unauthorized", code=401)
        state = request.json['state']
        db.set_config_state(state)
        return util.build_response("ok")


config_model = api.model('Drinklist init', {
    'adminName': fields.String,
    'adminPassword': fields.String,
    'modName': fields.String,
    'modPassword': fields.String,
    'users': fields.List(fields.String)
})


@api.route('/config/init')
class check_config_state(Resource):
    @api.doc(body=config_model)
    def post(self):
        """
        config the drinklist
        """
        if db.get_config_state() != 0 and not is_admin():
            return util.build_response("unauthorized", code=401)
        adminName = request.json['adminName']
        adminPassword = request.json['adminPassword']
        modName = request.json['modName']
        modPassword = request.json['modPassword']
        usernames = request.json['users']

        db.set_config_state(1)
        db.change_user_name(1, adminName)
        db.change_user_password(1, adminPassword)

        db.change_user_name(2, modName)
        db.change_user_password(2, modPassword)

        for user in usernames:
            db.add_user(name=user, money=0,
                        password=util.standard_user_password, alias=user)

        return util.build_response("ok")


@api.route('/webhooks/releases')
class webhook_releases(Resource):
    @api.doc()
    def post(self):
        """
        receive release informations
        """
        tag_name = request.json['release']['tag_name']
        tag_description = request.json['release']['name']
        open_issues = request.json['repository']['open_issues']
        is_draft = request.json['release']['draft']
        if not is_draft:
            db.set_current_release(
                {"release_tag": tag_name, "release_message": tag_description, "open_issues": open_issues})
            return util.build_response("ok")

        else:
            return util.build_response("release is a draft", code=412)

    @api.doc()
    def get(self):
        """
        get release informations
        """
        return util.build_response(db.get_repo_information())


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
