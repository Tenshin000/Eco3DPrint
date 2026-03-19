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
    def __init__(self, database=None, on_change_callback=None, print_manager_callback=None):
        """
        Initialize the monitoring service.

        :param database: Database instance used to persist node status updates.
                         If None, a new database connection is created from
                         configuration parameters.
        :param on_change_callback: A callback function passed to the NodeMonitor 
                                   that is executed when the state of the nodes changes. 
        :param print_manager_callback: Optional callback used to notify the
                               PrintManager when a node status changes
                               (e.g., OFFLINE, ONLINE, PRINTING). It is
                               invoked with the parameters
                               (ip, old_status, new_status) so the
                               PrintManager can react accordingly
                               (for example reassigning or stopping jobs).
        """
        self.on_change_callback = on_change_callback
        self.print_manager_callback = print_manager_callback # Hook for PrintManager
        
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
        
        # In-memory structure storing node information
        # Format:
        # {
        #   "node_ip": {
        #       "name": "...",
        #       "type": "...",
        #       "utilization": "...",
        #       "status": "ONLINE"
        #   }
        # }
        self._nodes = {}
        # Thread lock used to safely access shared data
        self._lock = threading.Lock()
        # Logger used to record monitoring activity
        self._logger = Log("node_monitor", "MONITOR").get_logger()
        # Control flag for watchdog thread
        self._running = True

        # Take the Nodes data from the Database
        self._load_initial_state()

        # Start background watchdog thread responsible for health checks
        self._watchdog_thread = threading.Thread(target=self._watchdog_loop, daemon=True)
        self._watchdog_thread.start()

    def register_node(self, ip, name, node_type, node_utilization):
        """
        Register or update a node when it connects to the system.

        This method is called by the node registration resource when
        a node performs login or initial registration. It forces the node
        back to an ONLINE state, meaning the watchdog will start pinging it again.

        :param ip: IPv6 address of the node
        :param name: Node alias/name
        :param node_type: Node type (e.g., printer model)
        :param node_utilization: Current usage state of the node
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
                "status": "ONLINE"
            }
        
        self._logger.info(f"Node {ip} ({name}) registered and marked ONLINE.")
        self._update_db_status(ip, "ONLINE")
        
        if old_status != "ONLINE" and self.print_manager_callback:
            self.print_manager_callback(ip, old_status, "ONLINE")
        
        if self.on_change_callback:
            self.on_change_callback(self.get_all_nodes())

    def set_node_online(self, ip):
        """Explicitly returns a node to the ONLINE state."""
        status_changed = False
        with self._lock:
            if ip in self._nodes and self._nodes[ip]["status"] != "ONLINE":
                self._nodes[ip]["status"] = "ONLINE"
                status_changed = True
                
        if status_changed:
            self._update_db_status(ip, "ONLINE")
            self._logger.info(f"Node {ip} returned to ONLINE state.")
            if self.on_change_callback:
                self.on_change_callback(self.get_all_nodes())

    def set_node_printing(self, ip):
        """Called by PrintManager when a print job is successfully acknowledged."""
        needs_update = False
        with self._lock:
            if ip in self._nodes:
                self._nodes[ip]["status"] = "PRINTING"
                needs_update = True
                
        if needs_update:
            self._update_db_status(ip, "PRINTING")
            self._logger.info(f"Node {ip} is now in PRINTING state.")
            
            if self.on_change_callback:
                self.on_change_callback(self.get_all_nodes())

    def set_node_offline(self, ip):
        """
        Explicitly mark a node as OFFLINE.
        
        This method is typically called when a node gracefully notifies the server
        that it is shutting down (e.g., via the /offSignal endpoint), bypassing
        the need for the watchdog to detect the timeout.

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
            self._logger.info(f"Node {ip} explicitly reported shutdown. State changed from {old_status_val} to OFFLINE.")
            
            # Update the database
            self._update_db_status(ip, "OFFLINE")
            
            # Trigger callbacks to notify the rest of the system (PrintManager, Frontend)
            if self.print_manager_callback:
                self.print_manager_callback(ip, old_status_val, "OFFLINE")
            if self.on_change_callback:
                self.on_change_callback(self.get_all_nodes())
        else:
            self._logger.debug(f"Received explicit offline signal for node {ip}, but it was already OFFLINE or unknown.")

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
        while self._running:
            # Responsive sleep loop. Checks self._running every second instead of blocking entirely for 60 seconds, allowing faster graceful shutdown.
            for _ in range(60):
                if not self._running:
                    return
                time.sleep(1)
            
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
                response = client.get("/health", timeout=60)
                
                # Determine node availability based on expected response
                if response is not None and response.payload in ["ONLINE", "PRINTING"]:
                    is_alive = True
                    reported_state = response.payload
                    break # The node satisfied the check, exit the retry loop immediately
                else:
                    self._logger.debug(f"Ping unsatisfied for {ip} on attempt {attempt + 1}. Payload: {getattr(response, 'payload', 'None')}")

            except Exception as e:
                # Log communication failure
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
            
            if self.print_manager_callback:
                self.print_manager_callback(ip, old_status_val, new_status_val)
            if self.on_change_callback:
                self.on_change_callback(self.get_all_nodes())
    
    def _update_db_status(self, ip, status):
        """
        Update the status of a node in the database.

        :param ip: Node IP address
        :param status: New node status (ONLINE or OFFLINE)
        """
        if self._db:
            query = "UPDATE Node SET status=%s WHERE ip=%s"
            self._db.execute(query, (status, ip))

    def _load_initial_state(self):
        """Load existing nodes from the database on startup."""
        self._logger.info("Loading existing nodes from database...")
        if self._db:
            try:
                # Retrieve all nodes from DB
                query = "SELECT ip, name, type, utilization, status FROM Node"
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
                                    "status": row.get('status', 'OFFLINE')
                                }
                            else:
                                ip = row[0]
                                self._nodes[ip] = {
                                    "name": row[1],
                                    "type": row[2],
                                    "utilization": row[3],
                                    "status": row[4]
                                }
                    self._logger.info(f"Loaded {len(self._nodes)} nodes from database.")
                    
                    # We immediately notify via WebSocket that we have loaded the nodes
                    if self.on_change_callback:
                        self.on_change_callback(self.get_all_nodes())
                        
            except Exception as e:
                self._logger.error(f"Failed to load initial nodes from DB: {e}")