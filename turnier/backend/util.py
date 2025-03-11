from flask import Response
import json
import os
import datetime
import time
import requests
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives import serialization
import base64

cookie_expire = int(os.environ.get("COOKIE_EXPIRE_TIME")) * \
    60*60 if os.environ.get("COOKIE_EXPIRE_TIME") else 60**3
domain = os.environ.get("DOMAIN") if os.environ.get(
    "DOMAIN") else "127.0.0.1:3000"
logging_enabled = os.environ.get(
    "DEBUG") == "true" if os.environ.get("DEBUG") else False

token = os.environ.get("X_AUTH_TOKEN")
old_domain = os.environ.get("OLD_DOMAIN")

admin_username = os.environ.get("ADMIN_USERNAME") if os.environ.get(
    "ADMIN_USERNAME") else "admin"
admin_password = os.environ.get("ADMIN_PASSWORD") if os.environ.get(
    "ADMIN_PASSWORD") else "unsafe"

moderator_username = os.environ.get(
    "MOD_USERNAME") if os.environ.get("MOD_USERNAME") else "moderator"
moderator_password = os.environ.get(
    "MOD_PASSWORD") if os.environ.get("MOD_PASSWORD") else "unsafe"
standard_user_password = os.environ.get(
    "USER_PASSWORD") if os.environ.get("USER_PASSWORD") else "unsafe"
undo_timelimit = int(os.environ.get(
    "UNDO_TIMELIMIT")) if os.environ.get("UNDO_TIMELIMIT") else 1
default_drink_category = int(os.environ.get(
    "DEFAULT_DRINK_CATEGORY")) if os.environ.get("DEFAULT_DRINK_CATEGORY") else "Getränk"
use_alias = os.environ.get(
    "USE_ALIAS") == "true" if os.environ.get("USE_ALIAS") else True
auto_hide_days = int(os.environ.get(
    "AUTO_HIDE_DAYS")) if os.environ.get("AUTO_HIDE_DAYS") else None
password_hash_rounds = max(10000, int(os.environ.get(
    "PASSSWORD_HASH_ROUNDS"))) if os.environ.get("PASSSWORD_HASH_ROUNDS") else 500000

auth_cookie_memberID = os.environ.get(
    "AUTH_COOKIE_PREFIX") if os.environ.get("AUTH_COOKIE_PREFIX") else ""

mail_server = os.environ.get(
    "MAIL_SERVER") if os.environ.get("MAIL_SERVER") else None
mail_port = os.environ.get(
    "MAIL_PORT") if os.environ.get("MAIL_PORT") else 587
mail_email = os.environ.get(
    "MAIL_EMAIL") if os.environ.get("MAIL_EMAIL") else None
mail_username = os.environ.get(
    "MAIL_USERNAME") if os.environ.get("MAIL_USERNAME") else None
mail_password = os.environ.get(
    "MAIL_PASSWORD") if os.environ.get("MAIL_PASSWORD") else None
mail_postfix = os.environ.get(
    "MAIL_POSTFIX") if os.environ.get("MAIL_POSTFIX") else None

OIDC_CLIENT_ID = os.environ.get(
    "OIDC_CLIENT_ID") if os.environ.get("OIDC_CLIENT_ID") else None
OIDC_CLIENT_SECRET = os.environ.get(
    "OIDC_CLIENT_SECRET") if os.environ.get("OIDC_CLIENT_SECRET") else None
OIDC_REDIRECT_MAIN_PAGE = os.environ.get(
    "OIDC_REDIRECT_MAIN_PAGE") if os.environ.get("OIDC_REDIRECT_MAIN_PAGE") else "http://127.0.0.1:3000"
OIDC_AUTH_PATH = os.environ.get(
    "OIDC_AUTH_PATH") if os.environ.get("OIDC_AUTH_PATH") else None
OIDC_AUTH_TOKEN = os.environ.get(
    "OIDC_AUTH_TOKEN") if os.environ.get("OIDC_AUTH_TOKEN") else None
OIDC_AUTH_REDIRECT = os.environ.get(
    "OIDC_AUTH_REDIRECT") if os.environ.get("OIDC_AUTH_REDIRECT") else "http://127.0.0.1:5000/api/oidc-redirect"
OIDC_USER_INFO = os.environ.get(
    "OIDC_USER_INFO") if os.environ.get("OIDC_USER_INFO") else None
OIDC_USER_NEEDS_VERIFICATION = os.environ.get(
    "OIDC_USER_NEEDS_VERIFICATION") == "true" if os.environ.get("OIDC_USER_NEEDS_VERIFICATION") else True

CURRENT_VERSION = 3

tempfile_path = "tempfiles"
backup_file_name = "backup.json"

os.environ['TZ'] = 'Europe/Berlin'
time.tzset()


def build_response(message: object, code: int = 200, type: str = "application/json", cookieMemberID=None, cookieToken=None):
    """
    Build a flask response, default is json format
    """
    r = Response(response=json.dumps(message), status=code, mimetype=type)
    if cookieMemberID and cookieToken:
        r.set_cookie(f"{auth_cookie_memberID}memberID", str(cookieMemberID),
                     max_age=cookie_expire, samesite='Strict', secure=not logging_enabled)
        r.set_cookie(f"{auth_cookie_memberID}token", cookieToken,
                     max_age=cookie_expire, samesite='Strict', secure=not logging_enabled)

    return r


def log(prefix, message):
    if logging_enabled:
        time = datetime.datetime.now().strftime("%x %X")
        output_string = f"[{time}] {prefix} -> {message}"
        with open("log.txt", 'a+') as f:
            f.write(f"{output_string}\n")


def get_oidc_token(token_url, code, redirect_uri):
    client_auth = requests.auth.HTTPBasicAuth(
        OIDC_CLIENT_ID, OIDC_CLIENT_SECRET)
    post_data = {
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": redirect_uri
    }
    response = requests.post(token_url, auth=client_auth, data=post_data)
    token_json = response.json()
    return token_json["access_token"]


def get_user_info(access_token, resource_url):
    headers = {'Authorization': 'Bearer ' + access_token}
    response = requests.get(resource_url, headers=headers)
    return response.json()

# Function to load a private key from a PEM file


def load_private_key_from_pem(pem_path: str) -> rsa.RSAPrivateKey:
    with open(pem_path, 'rb') as pem_file:
        pem_data = pem_file.read()
        private_key = serialization.load_pem_private_key(
            pem_data,
            password=None,
        )
    return private_key


private_key: rsa.RSAPrivateKey = load_private_key_from_pem(
    'rsa-keys/private-key.pem')

# Function to sign a message using RSA private key


def sign_message(message: str) -> str:
    signature = private_key.sign(
        message.encode(),
        padding.PKCS1v15(),
        hashes.SHA256()
    )
    return base64.b64encode(signature).decode()


checkout_mail_text = """Hallo {name},
eine Getränkelisten abrechnung wurde durchgeführt, wir möchten dich hiermit über deinen aktuellen Kontostand informieren.
Aktuell hast du ein Guthaben von {balance}€.

Viele Grüße
"""

money_request_mail_test = """Hallo {name},
{requester} möchte eine Ausgabe von {money}€ mit dir teilen, gehe jetzt auf {url} um die Zahlung zu bestätigen.
"""
