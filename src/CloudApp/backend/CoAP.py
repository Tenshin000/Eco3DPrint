#!/usr/bin/env python
from coapthon.server.coap import CoAP
from coapthon.resources.resource import Resource

# Add the resource for registration
class RegisterResource(Resource):
    def __init__(self, name="RegisterResource", coap_server=None):
        super(RegisterResource, self).__init__(name, coap_server, visible=True, observable=False, allow_children=True)
        self.payload = ""

    def render_POST(self, request):
        client_ip = request.source[0]
        print(f"Registration request (POST) received from: {client_ip}")
        print(f"Payload received from node: {request.payload}")
        
        # Success response for the node
        self.payload = "Registration Success!"
        return self

class CoAPServer(CoAP):
    def __init__(self, host, port):
        super(CoAPServer, self).__init__((host, port), False)
        # Add the "register/" route
        self.add_resource("register/", RegisterResource())

# "::" allows listening on all available IPv6 interfaces. "0.0.0.0" would be used for IPv4.
def start_CoAP_server(host="::", port=5683):
    server = CoAPServer(host, port)
    print(f"Cloud CoAP Server listening on {host}:{port}...")
    try:
        server.listen(10)
    except KeyboardInterrupt:
        print("Server Shutdown.")
        server.close()

if __name__ == "__main__":
    start_CoAP_server()