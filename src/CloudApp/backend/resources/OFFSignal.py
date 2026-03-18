import coapthon.defines as defines

from coapthon.resources.resource import Resource

from .BasicResource import BasicResource
from ..Database import Database
from utility.Log import Log

class OFFSignal(Resource):
    """
    Resource class handling CoAP DELETE requests for graceful node shutdown.
    
    This class receives a signal from a node right before it turns OFF
    (e.g., due to a hard reset) and immediately notifies the NodeMonitor
    to update its status to OFFLINE, preventing unnecessary ping retries.
    """
    def __init__(self, name="OFF Signal", monitor=None):
        """
        Initialize the OFFSignal resource.

        :param name: The name of the CoAP resource (default is "OFF Signal").
        :param monitor: The shared NodeMonitor instance used to manage node states.
        """
        super().__init__(name, observable=False)
        self.payload = "OFF Signal Resource"
        
        # Initialize logger for this specific resource
        self._logger = Log(
            logger_name="off_signal_logger",
            module_name="CoAP_SERVER"
        ).get_logger()
        
        # We require a monitor instance to update the node's state
        if monitor is None:
            self._monitor = NodeMonitor(Database())
        else:
            self._monitor = monitor

        self._resource = BasicResource()

    def render_DELETE_advanced(self, request, response):
        """
        Process incoming CoAP DELETE requests.
        
        When a node sends a DELETE request to this endpoint, it signifies
        that the node is shutting down. The method extracts the sender's IP
        and uses the NodeMonitor to explicitly set the node's status to OFFLINE.

        :param request: The incoming CoAP request object.
        :param response: The outgoing CoAP response object to be manipulated.
        :return: A tuple containing the updated resource instance and the response object (self, response).
        """
        node_ip = request.source[0]
        
        self._logger.info(f"Received OFF signal (DELETE) from node {node_ip}:{request.source[1]}")
        
        if self._monitor is None:
            self._logger.error("NodeMonitor is missing. Cannot update node status.")
            response.code = defines.Codes.INTERNAL_SERVER_ERROR.number
            return self._resource, response

        # Use the monitor to forcefully set the node to OFFLINE
        self._monitor.set_node_offline(node_ip)
        
        self._logger.info(f"Successfully processed OFF signal. Node {node_ip} is now OFFLINE.")
        
        # Respond with 2.02 Deleted (Code 66) as acknowledgment
        response.code = defines.Codes.DELETED.number
        response.payload = "Node marked as OFFLINE."
        
        return self._resource, response