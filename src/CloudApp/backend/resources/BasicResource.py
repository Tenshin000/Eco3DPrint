from coapthon.resources.resource import Resource

class BasicResource(Resource):
    def __init__(self, name="BasicResource", coap_server=None):
        super(BasicResource, self).__init__(name, coap_server, visible=True,
                                            observable=True, allow_children=True)

        self.payload = "Basic Resource"
        self.resource_type = "rt1"
        self.content_type = "text/plain"
        self.interface_type = "if1"

    def render_GET(self, request):
        return self

    def render_POST(self, request):
        res = BasicResource()
        res.payload = request.payload
        return res

    def render_PUT(self, request):
        self.payload = request.payload
        return self

    def render_DELETE(self, request):
        return True