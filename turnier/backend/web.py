from flask_sqlalchemy import SQLAlchemy
from flask import Flask
from flask_cors import CORS
import os

app = Flask(__name__)
CORS(app, supports_credentials=True)
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
os.makedirs("instance/db", exist_ok=True)
if os.environ.get("DB_CONNECTION"):
    app.config['SQLALCHEMY_DATABASE_URI'] = os.environ.get("DB_CONNECTION")
else:
    app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///db/database.db'

sql_database = SQLAlchemy(app)
