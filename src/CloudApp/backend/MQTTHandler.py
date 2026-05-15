import json
import paho.mqtt.client as mqtt

from configparser import ConfigParser

from backend.Database import Database
from utility.Log import Log

class MQTTHandler:
    """
    Component for handling MQTT subscriptions and processing incoming telemetry data.
    
    This class connects to an MQTT broker, subscribes to the measurements topic,
    parses incoming JSON payloads from the nodes, retrieves the active print_id,
    and stores the measurements into the database.
    """
    # The topic uses the MQTT wildcard '+' to match ANY sensor IP dynamically.
    def __init__(self, broker_host="127.0.0.1", broker_port=1883, database=None, topic="+/print/measurements"):
        """
        Initialize the MQTT Handler.

        :param broker_host: The hostname or IP of the MQTT broker.
        :param broker_port: The port of the MQTT broker (usually 1883).
        :param database: Database instance used to persist the measurements. 
        :param topic: The MQTT topic to subscribe to. Uses '+' wildcard to catch all IPs.
        """
        self._broker_host = broker_host
        self._broker_port = broker_port
        self._topic = topic
        
        # Initialize a dedicated logger
        self._logger = Log(logger_name="mqtt_logger", module_name="MQTT_SERVER").get_logger()
        
        # If no database instance is provided, create one using configuration
        if database is None:
            config = ConfigParser()
            config.read('./backend/config.ini')
            self._db = Database(
                host=config.get('mysql', 'host'),
                user=config.get('mysql', 'user'),
                password=config.get('mysql', 'password'),
                database=config.get('mysql', 'database')
            )
            if not self._db.connect():
                exit(1)
        else:
            self._db = database

        # Initialize the Paho MQTT Client using the modern v2 API
        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        
        # Attach callbacks
        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message
        self._client.on_disconnect = self._on_disconnect
        self._logger.info(f"MQTT Server Initialized on [{broker_host}]:{broker_port}...")

    def start(self):
        """Connect to the broker and start the network loop in a background thread."""
        try:
            self._logger.info(f"Connecting to MQTT Broker at {self._broker_host}:{self._broker_port}...")
            self._client.connect(self._broker_host, self._broker_port, keepalive=60)
            # loop_start() creates a daemon thread automatically to handle network traffic
            self._client.loop_start()
        except Exception as e:
            self._logger.error(f"Failed to start MQTT Handler: {e}")

    def stop(self):
        """Stop the network loop and disconnect from the broker gracefully."""
        self._logger.info("Stopping MQTT Server...")
        self._client.loop_stop()
        self._client.disconnect()

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        """Callback triggered when the client connects to the broker."""
        if reason_code == 0:
            self._logger.info(f"Successfully connected to MQTT Broker. Subscribing to wildcard topic: {self._topic}")
            client.subscribe(self._topic)
        else:
            self._logger.error(f"Failed to connect to MQTT Broker. Reason code: {reason_code}")

    def _on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties):
        """Callback triggered when the client disconnects from the broker."""
        self._logger.warning(f"Disconnected from MQTT Broker. Reason code: {reason_code}")

    def _on_message(self, client, userdata, msg):
        """
        Callback triggered when a message is received on a subscribed topic.
        
        It extracts the sensor IP from the topic, maps it to the Printer IP,
        queries the DB for the active print_id, and inserts the measurement.
        """
        try:
            # 1. Extract the Sensor IP from the topic string "IP_SENSOR/print/measurements"
            topic_parts = msg.topic.split('/')
            sensor_ip = topic_parts[0]

            # Decode the payload
            payload_array = json.loads(msg.payload.decode())
            if not isinstance(payload_array, list):
                raise TypeError("MQTT payload is not a SenML array.")

            data = {}
            
            # Parse the SenML array for actual values 
            for item in payload_array:
                metric_name = item.get("n")
                if metric_name and "v" in item:
                    data[metric_name] = item["v"]
            
            # 2. Retrieve the IP of the Printer node based on the Sensor IP
            ip_query = "SELECT ip FROM Node WHERE sensor_ip=%s LIMIT 1"
            ip_result = self._db.execute(ip_query, (sensor_ip,))
            
            if not ip_result:
                self._logger.warning(f"Sensor IP '{sensor_ip}' not paired with any Printer. Dropping measurement.")
                return
                
            node_ip = ip_result[0].get('ip') if isinstance(ip_result[0], dict) else ip_result[0][0]

            # 3. Retrieve active print_id for the printer using the discovered IP
            query = "SELECT id FROM `Print` WHERE ip=%s ORDER BY id DESC LIMIT 1"
            result = self._db.execute(query, (node_ip,))
            
            print_id = None
            if result:
                print_id = result[0].get("id") if isinstance(result[0], dict) else result[0][0]
            else:
                # If no print job exists at all (e.g. simulated via hardware button), we allow a NULL print_id 
                # or create a dummy ID for testing, but let's just log and continue to prove it works
                self._logger.warning(f"No print job found for {node_ip}. Saving measurement with NULL print_id for testing.")

            # Insert the measurement into the database
            insert_query = """
                INSERT IGNORE INTO Measurement (
                    print_id, ip, X_Axis_Plate, Y_Axis_Plate, Z_Axis_Plate, 
                    X_Axis_Extrusion, Y_Axis_Extrusion, Z_Axis_Extrusion, Tension, Power
                ) VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
            """
            
            params = (
                print_id,
                node_ip,
                data.get("X_Axis_Plate"),
                data.get("Y_Axis_Plate"),
                data.get("Z_Axis_Plate"),
                data.get("X_Axis_Extrusion"),
                data.get("Y_Axis_Extrusion"),
                data.get("Z_Axis_Extrusion"),
                data.get("Tension"),
                data.get("Power")
            )

            insert_result = self._db.execute(insert_query, params)
            
            if insert_result is not None:
                self._logger.debug(f"Measurement processed for Node: {node_ip}")
            else:
                self._logger.error("Database query failed while inserting measurement.")
            
        except (json.JSONDecodeError, TypeError) as e:
            self._logger.error(f"Failed to parse MQTT message payload as SenML JSON: {e}")
        except Exception as e:
            self._logger.error(f"Unexpected error in MQTT message processing: {e}")