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
        self.connections={}

    def on_connect(self, client, server):
        self.active_connections += 1

    def on_disconnect(self, client, server):
        # Remove the clientid from the connections which is a mapping from organization to clientids
        for organization in self.connections:
            if client in self.connections[organization]:
                self.connections[organization].remove(client)
                print(client['id'],"Unsubscribed from organization")

        self.active_connections -= 1

    def on_recieve(self, client, server, message):
        parsed = json.loads(message)
        
        # If the action is subscribe add the clientid to the connections which is a mapping from organization to client objects
        if parsed["action"] == "subscribe":
            print(client['id'],"Subscribed to organization")
            if parsed["organization"] not in self.connections:
                self.connections[parsed["organization"]] = []
            self.connections[parsed["organization"]].append(client)
   
    def send(self,organization, message):
        if organization in self.connections:
            for client in self.connections[organization]:
                self.server.send_message(client, json.dumps(message))
            
        
        print("Ws message sent:",organization,"->", message)

    def trigger_reload_queue(self):
        self.send({"action": "reload_queue"})

    def trigger_reload_next(self):
        self.send({"action": "reload_current_song"})