import coapthon.defines as defines
import json

from coapthon.resources.resource import Resource
from configparser import ConfigParser

from backend.Database import Database
from backend.NodeMonitor import NodeMonitor
from utility.Log import Log

class NodeRegistration(Resource):
    """
    Resource class handling CoAP POST requests for IoT node registration.
    
    This class interfaces with a MySQL database to store, verify, or update 
    the metadata of connecting nodes based on their IPv6 address.
    """
    def __init__(self, name="Node Registration", coap_server=None, database=None, node_monitor=None):
        """
        Initialize the NodeRegistration resource.

        :param name: The name of the CoAP resource (default is "Node Registration").
        :param database: An optional Database instance. If None, a new connection 
                            is established using credentials from the configuration file.
        """
        super().__init__(name, observable=False)
        self.payload = "Node Registration"
        
        self._logger = Log(
            logger_name="registration_logger",
            module_name="CoAP_SERVER"
        ).get_logger()
        
        if database is None:
            # Load database configuration parameters
            config = ConfigParser()
            config.read('./backend/config.ini')
            db_host = config.get('mysql', 'host')
            db_user = config.get('mysql', 'user')
            db_password = config.get('mysql', 'password')
            db_name = config.get('mysql', 'database')
            
            self._db = Database(host=db_host, user=db_user, password=db_password, database=db_name)
            
            if not self._db.connect():
                self._logger.error("Fatal Error: Unable to connect to the database. Exiting application.")
                exit(1)
        else:
            self._db = database
        
        # Ensure a NodeMonitor instance is explicitly provided
        if node_monitor is None:
            self._logger.error("NodeMonitor instance is strictly required but was not provided.")
            raise ValueError("A NodeMonitor instance must be provided to CoAPServer.")
        self._monitor = node_monitor

    def render_POST_advanced(self, request, response):
        """
        Process incoming CoAP POST requests using the advanced interface.
        
        This method evaluates the JSON payload, verifies node existence in the 
        database, and formulates a specific CoAP response code based on the outcome.

        :param request: The incoming CoAP request object.
        :param response: The outgoing CoAP response object to be manipulated.
        :return: A tuple containing the updated resource instance and the response object (self, response).
        """
        # Preserve the original URI query in the response path
        response.location_query = request.uri_query
        
        self._logger.info(f"Received message {request.mid} from {request.source[0]}:{request.source[1]} (Token: {request.token})")
        
        # Payload Validation
        try:
            payload_array = json.loads(request.payload)
            if not isinstance(payload_array, list):
                raise TypeError("Payload must be a SenML array (list).")

            node_data = {"name": "Unknown", "type": "Unknown", "utilization": "Unknown", "sensor_ip": None}
            
            for item in payload_array:
                if "bn" in item:
                    node_data["name"] = item["bn"]
                
                metric_name = item.get("n")
                if metric_name == "type":
                    node_data["type"] = item.get("vs", "Unknown")
                elif metric_name == "utilization":
                    node_data["utilization"] = item.get("vs", "Unknown")
                elif metric_name == "sensor_ip":
                    s_ip = item.get("vs", "Unknown")
                    # Ensure we catch empty strings ("") or "NULL" to store an actual NULL in the database
                    node_data["sensor_ip"] = None if (s_ip == "NULL" or str(s_ip).strip() == "") else s_ip
                    
        except (json.JSONDecodeError, TypeError) as e:
            msg = str(e) if isinstance(e, TypeError) else e.msg
            self._logger.error(f"Payload validation failed: {msg}")
            response.code = defines.Codes.BAD_REQUEST.number
            response.payload = "Error: Invalid SenML JSON format."
            return self, response

        # Node Verification
        check_ip_query = "SELECT name FROM Node WHERE ip=%s"
        existing = self._db.execute(check_ip_query, (request.source[0],))
        
        if existing is None:
            self._logger.error("Database transaction failed during IP verification.")
            response.code = defines.Codes.INTERNAL_SERVER_ERROR.number
            response.payload = "Error: Internal database exception during verification."
            return self, response

        # Handle Existing Node (Login)
        if existing:
            # We must update all parameters, including sensor_ip in case it has changed
            update_query = "UPDATE Node SET name=%s, type=%s, utilization=%s, status='ONLINE', sensor_ip=%s WHERE ip=%s"
            if self._db.execute(update_query, (node_data["name"], node_data["type"], node_data["utilization"], node_data["sensor_ip"], request.source[0])) is None:
                self._logger.error(f"Failed to update alias/status for node {request.source[0]}.")
                response.code = defines.Codes.INTERNAL_SERVER_ERROR.number
                response.payload = "Error: Internal database exception during node update."
                return self, response
            
            # Register the node in the monitor with the updated sensor_ip
            self._monitor.register_node(request.source[0], node_data["name"], node_data["type"], node_data["utilization"], node_data["sensor_ip"])

            self._logger.info(f"Node {request.source[0]} successfully authenticated (Already registered).")
            response.code = defines.Codes.VALID.number # CoAP Code 2.03
            response.payload = "Success: Node already registered and validated."
            return self, response

        # Register New Node
        sql_query = """
            REPLACE INTO Node (ip, name, type, utilization, sensor_ip, status)
            VALUES (%s, %s, %s, %s, %s, %s)
        """
        sql_values = (
            request.source[0],
            node_data["name"],
            node_data["type"],
            node_data["utilization"],
            node_data["sensor_ip"],
            "ONLINE"
        )

        result = self._db.execute(sql_query, sql_values)

        if result is None:
            self._logger.error(f"Failed to insert metadata for new node {request.source[0]}.")
            response.code = defines.Codes.INTERNAL_SERVER_ERROR.number
            response.payload = "Error: Database insertion query failed."
            return self, response

        # Register the newly created node in the monitor
        self._monitor.register_node(request.source[0], node_data["name"], node_data["type"], node_data["utilization"], node_data["sensor_ip"])

        self._logger.info(f"Node {request.source[0]} successfully registered in the database.")
        response.code = defines.Codes.CREATED.number  # CoAP Code 2.01
        response.payload = "Success: Node registered successfully." 

        return self, response