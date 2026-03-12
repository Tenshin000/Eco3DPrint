from coapthon.resources.resource import Resource
from coapthon import defines

from .BasicResource import BasicResource
from backend.PrintManager import PrintManager
from utility.Log import Log

class PrintFinished(Resource):
    """
    CoAP resource handling end-of-print notifications from printing nodes.

    This resource processes PUT requests sent by nodes when a print job
    completes, fails, or encounters an error. The resource extracts the
    source IP and result payload from the request and passes it to the
    PrintManager to update the internal print job status and node state.
    
    Depending on the outcome of the processing, an appropriate CoAP response
    code is returned to the node:
        - 2.04 Changed: Notification successfully received and processed.
        - 4.00 Bad Request: The payload format or value is invalid.
        - 5.00 Internal Server Error: PrintManager failed to handle the result.
    """
    def __init__(self, name="Print Finished", print_manager=None):
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

        # Basic CoAP resource helper for response handling
        self._resource = BasicResource()

    def render_PUT_advanced(self, request, response):
        """
        Handle PUT requests from nodes indicating a finished print job.

        The request payload should indicate the print result, such as:
        "FINISHED", "FAILED", or "ERROR". The source IP of the node is used
        to identify the corresponding print job.

        The method performs the following steps:
            1. Preserve the original URI query in the response.
            2. Log incoming request metadata and payload.
            3. Validate the payload against expected print result values.
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

        # Extract source IP and payload result
        source_ip = request.source[0]
        result = request.payload

        # Validate payload and forward to PrintManager
        if result in ["FINISHED", "FAILED", "ERROR"]:
            if self._print_manager:
                state_correct = self._print_manager.handle_print_finished(source_ip, result)

                if state_correct:
                    response.code = defines.Codes.CHANGED.number  # 2.04 Changed
                    response.payload = "Notification Received"
                    self._logger.info(f"Sending a correct response to {source_ip}")
                    return self._resource, response
                else:
                    response.code = defines.Codes.INTERNAL_SERVER_ERROR.number  # 5.00 Internal Server Error
                    response.payload = "Server Error"
                    self._logger.info(f"Sending Internal Server Error to {source_ip}")
                    return self._resource, response
        else:
            response.code = defines.Codes.BAD_REQUEST.number  # 4.00 Bad Request
            response.payload = "Invalid Result Payload"
            self._logger.info(f"Sending Bad Request to {source_ip}")
            return self._resource, response
            