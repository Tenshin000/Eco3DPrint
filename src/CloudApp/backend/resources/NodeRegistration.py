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
        :param node_monitor: The NodeMonitor instance tracking health and states.
        """
        super().__init__(name, observable=False)
        self.payload = "Node Registration"
        
        self._logger = Log(
            logger_name="registration_logger",
            module_name="CoAP_SERVER"
        ).get_logger()
        
        # Database connection setup
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
                exit(1)
        else:
            self._db = database

        # Ensure NodeMonitor is available
        if node_monitor is None:
            self._logger.error("NodeMonitor instance is strictly required for Registration Resource.")
            exit(1)
        else:
            self._monitor = node_monitor

    # Using render_POST_advanced allows direct manipulation of the CoAP response object
    def render_POST_advanced(self, request, response):
        """
        Handle CoAP POST requests for node registration with advanced response control.
        """
        # 1. Parse the incoming SenML payload
        try:
            payload_str = request.payload
            senml_data = json.loads(payload_str)
        except Exception as e:
            self._logger.error(f"Invalid SenML payload from {request.source[0]}: {e}")
            response.code = defines.Codes.BAD_REQUEST.number
            response.payload = "Error: Invalid SenML payload."
            return self, response

        # Default fallback values
        node_data = {
            "name": "Unknown",
            "type": "Unknown",
            "utilization": "Unknown",
            "sensor_ip": "NULL"
        }

        # Extract values from SenML format
        for entry in senml_data:
            if "bn" in entry:
                node_data["name"] = entry["bn"]
            if "n" in entry:
                if entry["n"] == "type":
                    node_data["type"] = entry.get("vs", "Unknown")
                elif entry["n"] == "utilization":
                    node_data["utilization"] = entry.get("vs", "Unknown")
                elif entry["n"] == "sensor_ip":
                    node_data["sensor_ip"] = entry.get("vs", "NULL")

        # 1. Retrieve the current status of the node (if it already exists in the DB)
        query = "SELECT status FROM Node WHERE ip = %s"
        existing = self._db.execute(query, (request.source[0],))
        
        current_status = "ONLINE"
        is_new_node = True
        
        if existing and len(existing) > 0 and existing[0]['status'] != "OFFLINE":
            # Do not interrupt an active print by forcefully overwriting it to 'ONLINE'.
            current_status = existing[0]['status'] 
            is_new_node = False
            self._logger.info(f"Node {request.source[0]} is updating parameters. Preserving state: {current_status}")

        # 2. Register New Node
        sql_query = """
            INSERT INTO Node (ip, name, type, utilization, sensor_ip, status)
            VALUES (%s, %s, %s, %s, %s, %s)
            ON DUPLICATE KEY UPDATE 
            name = VALUES(name), type = VALUES(type), utilization = VALUES(utilization), 
            sensor_ip = VALUES(sensor_ip), status = VALUES(status)
        """
        
        sql_values = (
            request.source[0],
            node_data["name"],
            node_data["type"],
            node_data["utilization"],
            node_data["sensor_ip"],
            current_status
        )

        result = self._db.execute(sql_query, sql_values)

        if result is None:
            self._logger.error(f"Failed to insert/update metadata for node {request.source[0]}.")
            response.code = defines.Codes.INTERNAL_SERVER_ERROR.number
            response.payload = "Error: Database insertion/update query failed."
            return self, response

        # 3. Update the NodeMonitor smartly without triggering fake reboots
        if is_new_node:
            # If it's a new node, initialize it in the Monitor and start the health_check
            self._monitor.register_node(request.source[0], node_data["name"], node_data["type"], node_data["utilization"], node_data["sensor_ip"])
            
            self._logger.info(f"Node {request.source[0]} successfully registered in the database.")
            response.code = defines.Codes.CREATED.number  # CoAP Code 2.01
            response.payload = "Success: Node registered successfully."
        else:
            # If it already existed (e.g., mid-print CoAP update), ONLY update the sensor_ip in memory!
            # Without calling "register_node()", the PrintManager will not interpret this as a reboot/crash.
            if request.source[0] in self._monitor._nodes:
                self._monitor._nodes[request.source[0]]["sensor_ip"] = node_data["sensor_ip"]
            
            response.code = defines.Codes.VALID.number # CoAP Code 2.03
            response.payload = "Success: Node parameters successfully updated."

        return self, response