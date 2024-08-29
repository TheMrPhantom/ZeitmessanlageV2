import json
import os
from websocket_server import WebsocketServer


class Websocket:

    def __init__(self) -> None:
        self.PORT = int(os.environ.get("websocket_port")) if os.environ.get(
            "websocket_port") else int(9001)
        self.server = WebsocketServer(host='0.0.0.0', port=self.PORT)
        self.server.set_fn_new_client(self.on_connect)
        self.server.set_fn_client_left(self.on_disconnect)
        self.server.set_fn_message_received(self.on_recieve)
        self.server.run_forever(threaded=True)
        self.active_connections = 0

    def on_connect(self, client, server):
        print(f"New Client with id {client['id']}")
        self.active_connections += 1

    def on_disconnect(self, client, server):
        print(f"Client disconnect with id {client['id']}")
        self.active_connections -= 1

    def on_recieve(self, client, server, message):
        print(f"Client with id {client['id']} sent: {message}")
        self.server.send_message_to_all(message)

    def send(self, message):
        self.server.send_message_to_all(json.dumps(message))
        print("Ws message sent:", message)

    def trigger_reload_queue(self):
        self.send({"action": "reload_queue"})

    def trigger_reload_next(self):
        self.send({"action": "reload_current_song"})