import json

from coapthon.resources.resource import Resource
from coapthon import defines

from backend.PrintManager import PrintManager
from utility.Log import Log

class PrintFinished(Resource):
    """
    CoAP resource handling end-of-print notifications from printing nodes.

    This resource processes PUT requests sent by nodes when a print job
    completes, fails, or encounters an error. The resource extracts the
    source IP, result payload, and energy consumed from the request, 
    then passes it to the PrintManager to update the internal print job 
    status and node state.
    
    Depending on the outcome of the processing, an appropriate CoAP response
    code is returned to the node:
        - 2.04 Changed: Notification successfully received and processed.
        - 4.00 Bad Request: The payload format or value is invalid.
        - 5.00 Internal Server Error: PrintManager failed to handle the result.
    """
    def __init__(self, name="Print Finished", coap_server=None, print_manager=None):
        """
        Initialize the PrintFinished CoAP resource.

        :param name: Name of the resource (default: "Print Finished").
        :param print_manager: Optional PrintManager instance. If not provided,
                              a new instance is created. The PrintManager is
                              responsible for updating job states and node
                              status based on the finished print notifications.
        """
        super().__init__(name, observable=False)
        self.payload = "Print Finished Resource"

        # Assign the provided PrintManager or create a new one
        if print_manager is None:
            print_manager = PrintManager()
        else:
            self._print_manager = print_manager

        # Logger for monitoring incoming notifications and responses
        self._logger = Log(
            logger_name="print_finished_logger",
            module_name="CoAP_SERVER"
        ).get_logger()

    def render_POST_advanced(self, request, response):
        """
        Handle POST requests from nodes indicating a finished print job.

        The request payload should be a JSON containing the print status 
        ("FINISHED", "FAILED", or "ERROR") and the accumulated energy.
        The source IP of the node is used to identify the corresponding print job.

        The method performs the following steps:
            1. Preserve the original URI query in the response.
            2. Log incoming request metadata and payload.
            3. Parse the JSON payload to extract 'status' and 'energy'.
            4. Call the PrintManager to update the job and node state.
            5. Set an appropriate CoAP response code based on processing outcome.

        :param request: The incoming CoAP request object.
        :param response: The CoAP response object to return to the node.
        :return: A tuple containing the resource instance and the response object.
        """
        # Preserve the original URI query in the response path
        response.location_query = request.uri_query

        # Log the request details
        self._logger.info(f"Received message {request.mid} from {request.source[0]}:{request.source[1]} (Token: {request.token})")
        self._logger.info(f"Payload content: {request.payload}")

        # Extract source IP
        source_ip = request.source[0]
        
        try:
            # Parse the incoming JSON payload
            payload_data = json.loads(request.payload)
            status = payload_data.get("status")
            # Using get with default 0.0 to prevent errors if energy key is missing
            energy = float(payload_data.get("energy", 0.0))
            
            # Validate payload status and forward to PrintManager
            if status in ["FINISHED", "FAILED", "ERROR"]:
                if self._print_manager:
                    state_correct = self._print_manager.handle_print_finished(source_ip, status, energy)

                    if state_correct:
                        response.code = defines.Codes.CHANGED.number  # 2.04 Changed
                        response.payload = "Notification Received"
                        self._logger.info(f"Sending a correct response to {source_ip}")
                        return self, response
                    else:
                        response.code = defines.Codes.INTERNAL_SERVER_ERROR.number  # 5.00 Internal Server Error
                        response.payload = "Server Error"
                        self._logger.info(f"Sending Internal Server Error to {source_ip}")
                        return self, response
            else:
                response.code = defines.Codes.BAD_REQUEST.number  # 4.00 Bad Request
                response.payload = "Invalid Result Payload Status"
                self._logger.info(f"Sending Bad Request to {source_ip}: Invalid Status")
                return self, response

        except json.JSONDecodeError:
            response.code = defines.Codes.BAD_REQUEST.number  # 4.00 Bad Request
            response.payload = "Invalid JSON Format"
            self._logger.info(f"Sending Bad Request to {source_ip}: Failed to parse JSON")
            return self, response
        except ValueError:
            response.code = defines.Codes.BAD_REQUEST.number  # 4.00 Bad Request
            response.payload = "Invalid Energy Value"
            self._logger.info(f"Sending Bad Request to {source_ip}: Invalid Energy float")
            return self, response