import concurrent.futures 
import threading
import time

from coapthon.client.helperclient import HelperClient
from configparser import ConfigParser

from backend.Database import Database
from utility.Log import Log

class NodeMonitor:
    """
    Component responsible for monitoring the health status of registered nodes.

    The monitor keeps an in-memory structure of active nodes and periodically
    checks their availability by sending CoAP health requests. 
    It pings nodes that are currently marked as ONLINE or PRINTING. If a node 
    fails to respond correctly after a set number of retries, it is marked OFFLINE 
    in memory and in the database, and is no longer pinged.
    """
    def __init__(self, database=None):
        """
        Initialize the monitoring service.

        :param database: Database instance used to persist node status updates.
                         If None, a new database connection is created from
                         configuration parameters.
        """        
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

        # Event listeners dictionary for the Observer pattern
        self._listeners = {
            'nodes_updated': [],       # Triggered when the general node list changes
            'node_status_changed': []  # Triggered when a specific node changes its status
        }
        
        # In-memory structure storing node information
        # Format:
        # {
        #   "node_ip": {
        #       "name": "...",
        #       "type": "...",
        #       "utilization": "...",
        #       "status": "ONLINE",
        #       "sensor_ip": "..."
        #   }
        # }
        self._nodes = {}
        # Thread lock used to safely access shared data
        self._lock = threading.Lock()
        # Logger used to record monitoring activity
        self._logger = Log("node_monitor", "MONITOR").get_logger()
        
        # Event used to cleanly stop the watchdog thread, replacing the old _running boolean flag
        self._stop_event = threading.Event()

        # Take the Nodes data from the Database
        self._load_initial_state()

        # Start background watchdog thread responsible for health checks
        self._watchdog_thread = threading.Thread(target=self._watchdog_loop, daemon=True)
        self._watchdog_thread.start()

    def stop(self):
        """
        Signal the watchdog thread to terminate immediately.
        This safely unblocks the sleep loop and exits the thread.
        """
        self._stop_event.set()

    def subscribe(self, event_name, callback):
        """
        Subscribe an external component to a specific event.
        
        :param event_name: String representing the event (e.g., 'nodes_updated')
        :param callback: Function to be executed when the event occurs
        """
        if event_name in self._listeners:
            self._listeners[event_name].append(callback)
        else:
            self._logger.warning(f"Attempted to subscribe to an unknown event: {event_name}")

    def _emit(self, event_name, *args, **kwargs):
        """
        Execute all callbacks subscribed to a specific event safely.
        """
        for callback in self._listeners.get(event_name, []):
            try:
                callback(*args, **kwargs)
            except Exception as e:
                self._logger.error(f"Error executing callback for event '{event_name}': {e}")

    def _load_initial_state(self):
        """Load existing nodes from the database on startup."""
        self._logger.info("Loading existing nodes from database...")
        if self._db:
            try:
                # Retrieve all nodes from DB, including sensor_ip
                query = "SELECT ip, name, type, utilization, status, sensor_ip FROM Node"
                records = self._db.execute(query) 

                if records:
                    with self._lock:
                        for row in records:
                            # We handle both the case where the DB returns tuples (default) or dictionaries
                            if isinstance(row, dict):
                                ip = row.get('ip')
                                self._nodes[ip] = {
                                    "name": row.get('name', 'Unknown'),
                                    "type": row.get('type', 'Unknown'),
                                    "utilization": row.get('utilization', 'Idle'),
                                    "status": row.get('status', 'OFFLINE'),
                                    "sensor_ip": row.get('sensor_ip', None)
                                }
                            else:
                                ip = row[0]
                                self._nodes[ip] = {
                                    "name": row[1],
                                    "type": row[2],
                                    "utilization": row[3],
                                    "status": row[4],
                                    "sensor_ip": row[5]
                                }
                    self._logger.info(f"Loaded {len(self._nodes)} nodes from database.")
                    
                    # Emit event immediately after loading state
                    self._emit('nodes_updated', self.get_all_nodes())
                        
            except Exception as e:
                self._logger.error(f"Failed to load initial nodes from DB: {e}")

    def register_node(self, ip, name, node_type, node_utilization, sensor_ip=None):
        """
        Register or update a node when it connects to the system.

        This method is called by the node registration resource when
        a node performs login or initial registration. It forces the node
        back to an ONLINE state, meaning the watchdog will start pinging it again.

        :param ip: IPv6 address of the node
        :param name: Node alias/name
        :param node_type: Node type (e.g., printer model)
        :param node_utilization: Current usage state of the node
        :param sensor_ip: IPv6 address of the paired sensor
        """
        # Protect shared structure from concurrent access
        old_status = "OFFLINE"
        with self._lock:
            if ip in self._nodes:
                old_status = self._nodes[ip]["status"]
                
            self._nodes[ip] = {
                "name": name,
                "type": node_type,
                "utilization": node_utilization,
                "status": "ONLINE",
                "sensor_ip": sensor_ip
            }
        
        sensor_log = sensor_ip if sensor_ip else "None"
        self._logger.info(f"Node {ip} ({name}) registered. Sensor IP: {sensor_log}. Marked ONLINE.")
        self._update_db_status(ip, "ONLINE")
        
        # Emit events to all subscribers
        if old_status != "ONLINE":
            self._emit('node_status_changed', ip, old_status, "ONLINE")
        self._emit('nodes_updated', self.get_all_nodes())

    def set_node_online(self, ip):
        """
        Explicitly returns a node to the ONLINE state.
        :param ip: IPv6 address of the node
        """
        status_changed = False
        old_status = None
        with self._lock:
            if ip in self._nodes and self._nodes[ip]["status"] != "ONLINE":
                old_status = self._nodes[ip]["status"]
                self._nodes[ip]["status"] = "ONLINE"
                status_changed = True
                
        if status_changed:
            self._update_db_status(ip, "ONLINE")
            self._logger.info(f"Node {ip} returned to ONLINE state.")
            
            # Emit events to all subscribers
            self._emit('node_status_changed', ip, old_status, "ONLINE")
            self._emit('nodes_updated', self.get_all_nodes())

    def set_node_printing(self, ip):
        """
        Called by PrintManager when a print job is successfully acknowledged.
        :param ip: IPv6 address of the node
        """
        needs_update = False
        old_status = None
        with self._lock:
            if ip in self._nodes:
                old_status = self._nodes[ip]["status"]
                self._nodes[ip]["status"] = "PRINTING"
                needs_update = True
                
        if needs_update:
            self._update_db_status(ip, "PRINTING")
            self._logger.info(f"Node {ip} is now in PRINTING state.")
            
            # Emit events to all subscribers
            self._emit('node_status_changed', ip, old_status, "PRINTING")
            self._emit('nodes_updated', self.get_all_nodes())

    def set_node_offline(self, ip):
        """
        Explicitly mark a node as OFFLINE.
        :param ip: IPv6 address of the node
        """
        status_changed = False
        old_status_val = None
        
        with self._lock:
            if ip in self._nodes:
                old_status = self._nodes[ip]["status"]
                if old_status != "OFFLINE":
                    self._nodes[ip]["status"] = "OFFLINE"
                    status_changed = True
                    old_status_val = old_status

        if status_changed:
            self._logger.info(f"Node {ip} explicitly reported shutdown. State changed to OFFLINE.")
            
            # Update the database
            self._update_db_status(ip, "OFFLINE")
            
            # Emit events to all subscribers
            self._emit('node_status_changed', ip, old_status_val, "OFFLINE")
            self._emit('nodes_updated', self.get_all_nodes())

    def get_all_nodes(self):
        """
        Return a safe copy of the current node list.

        This prevents external components (e.g., frontend handlers)
        from modifying the internal monitoring structure.

        :return: Copy of the node dictionary
        """
        with self._lock:
            return dict(self._nodes)

    def _watchdog_loop(self):
        """
        Background monitoring loop.

        Runs continuously in a separate thread and periodically checks
        the health of registered nodes that are currently ONLINE.
        """
        self._logger.info("Watchdog thread started. Monitoring node health...")
        
        # Replaced the old boolean flag and for-loop sleep with an Event wait mechanism.
        # This will block for 60 seconds unless self._stop_event is set, in which case it returns True instantly.
        while not self._stop_event.is_set():
            # Wait for 60 seconds. If the stop event is triggered during this time, exit the loop immediately.
            if self._stop_event.wait(60.0):
                return
            
            # Extract list of ONLINE and PRINTING node IPs while holding the lock briefly
            with self._lock:
                ips_to_check = [ip for ip, data in self._nodes.items() if data["status"] in ["ONLINE", "PRINTING"]]
            
            # Ping nodes concurrently using a ThreadPool. This prevents one unresponsive node from delaying the health checks of all other nodes.
            if ips_to_check:
                with concurrent.futures.ThreadPoolExecutor(max_workers=min(10, len(ips_to_check))) as executor:
                    for ip in ips_to_check:
                        executor.submit(self._ping_node, ip)

    def _ping_node(self, ip):
        """
        Send a health-check request to a node.

        The node is expected to respond to the /health endpoint with
        a payload containing the string "ONLINE" or "PRINTING".
        If the node fails to respond or the response is not satisfactory,
        the system will retry up to 2 more times (3 total attempts).
        If all attempts fail, it is marked OFFLINE.

        :param ip: IP address of the node to check
        """
        is_alive = False
        reported_state = None
        max_attempts = 3

        # Try up to 3 times (1 initial attempt + 2 retries)
        for attempt in range(max_attempts):
            client = None
            try:
                # Create temporary CoAP client targeting the node
                client = HelperClient(server=(ip, 5683))
                # Send health-check request
                response = client.get("/health", timeout=15)
                
                # Determine node availability based on expected response
                if response is not None and response.payload in ["ONLINE", "PRINTING"]:
                    is_alive = True
                    reported_state = response.payload
                    break # The node satisfied the check, exit the retry loop immediately
                else:
                    self._logger.debug(f"Ping unsatisfied for {ip} on attempt {attempt + 1}.")

            except Exception as e:
                # Communication failure
                self._logger.debug(f"Ping failed for {ip} on attempt {attempt + 1}: {e}")
            finally:
                # Ensure CoAP client is properly closed
                if client:
                    client.stop()
            
            # If not alive and we still have attempts left, wait briefly before retrying
            if not is_alive and attempt < max_attempts - 1:
                time.sleep(1) 

        # Update node status in memory and database if necessary
        status_changed = False
        old_status_val = None
        new_status_val = None
        
        with self._lock:
            # Make sure the node still exists in our dict (hasn't been deleted elsewhere)
            if ip in self._nodes:
                old_status = self._nodes[ip]["status"]
                
                if is_alive:
                    # If the node is alive but its state has changed (e.g. it has finished printing)
                    if old_status != reported_state:
                        self._nodes[ip]["status"] = reported_state
                        status_changed = True
                        old_status_val = old_status
                        new_status_val = reported_state
                else:
                    # If it doesn't respond, it goes OFFLINE
                    if old_status in ["ONLINE", "PRINTING"]:
                        self._nodes[ip]["status"] = "OFFLINE"
                        status_changed = True
                        old_status_val = old_status
                        new_status_val = "OFFLINE"
        
        if status_changed:
            if is_alive:
                self._logger.info(f"Node {ip} state changed from {old_status_val} to {new_status_val}.")
            else:
                self._logger.warning(f"Node {ip} failed all {max_attempts} ping attempts. Marked OFFLINE.")
                
            self._update_db_status(ip, new_status_val)
            
            # Emit events to all subscribers
            self._emit('node_status_changed', ip, old_status_val, new_status_val)
            self._emit('nodes_updated', self.get_all_nodes())
    
    def _update_db_status(self, ip, status):
        """
        Update the status of a node in the database.

        :param ip: Node IP address
        :param status: New node status (ONLINE or OFFLINE)
        """
        if self._db:
            query = "UPDATE Node SET status=%s WHERE ip=%s"
            self._db.execute(query, (status, ip))