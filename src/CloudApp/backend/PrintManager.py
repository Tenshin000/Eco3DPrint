import os
import math
import threading

from coapthon import defines
from coapthon.client.helperclient import HelperClient
from coapthon.messages.request import Request
from coapthon.messages.option import Option

from backend.NodeMonitor import NodeMonitor
from utility.Log import Log

class PrintManager:
    """
    Component responsible for managing the 3D printer queue and 
    handling the CoAP block-wise transfer of STL files to the nodes.
    """
    def __init__(self, database=None, node_monitor=None):
        """
        Initialize the PrintManager.

        :param database: Database instance for queue management.
        :param node_monitor: NodeMonitor instance to update node states.
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
        if node_monitor is None:
            self._monitor = NodeMonitor()
        else: 
            self._monitor = node_monitor
        self._logger = Log("print_manager", "PRINT_MANAGER").get_logger()
        
        # Register this manager to receive status change events from NodeMonitor
        self._monitor.print_manager_callback = self.on_node_status_change

    def add_print_job(self, ip, original_stl_name):
        """
        Add a new print job to the database queue. If the node is ONLINE,
        it immediately attempts to start the print.
        """
        query = "INSERT INTO Print (ip, stl_name, status) VALUES (%s, %s, %s)"
        self._db.execute(query, (ip, original_stl_name, "QUEUE"))
        self._logger.info(f"Added print job for {ip} (File: {original_stl_name}) to QUEUE.")

        # Check if the node is currently available to print
        nodes = self._monitor.get_all_nodes()
        if ip in nodes and nodes[ip].get('status') == 'ONLINE':
            self._check_and_start_print(ip)

    def on_node_status_change(self, ip, old_status, new_status):
        """Callback triggered by NodeMonitor when a node changes its status."""
        if new_status == 'ONLINE':
            # Node came back online, check if there are pending jobs in QUEUE
            self._check_and_start_print(ip)
            
        elif old_status == 'PRINTING' and new_status == 'OFFLINE':
            # Node disconnected during a print. Mark the active print as ERROR.
            query = "UPDATE Print SET status = 'ERROR' WHERE ip = %s AND status = 'PRINTING'"
            self._db.execute(query, (ip,))
            self._logger.warning(f"Node {ip} went OFFLINE during PRINTING. Job marked as ERROR.")

    def _check_and_start_print(self, ip):
        """
        Checks the database for the oldest 'QUEUE' job for a specific IP.
        If found, starts a background thread to send the file.
        """
        # Let's check that there are no active or transferring jobs for this IP already.
        check_query = "SELECT id FROM Print WHERE ip = %s AND status = 'PRINTING'"
        if self._db.execute(check_query, (ip,)):
            self._logger.warning(f"Node {ip} is already processing a job. Keeping the rest in QUEUE.")
            return

        query = "SELECT id, stl_name FROM Print WHERE ip = %s AND status = 'QUEUE' ORDER BY id ASC LIMIT 1"
        records = self._db.execute(query, (ip,))
        
        if records:
            # Handle both dictionary and tuple return types from DB
            job_id = records[0].get('id') if isinstance(records[0], dict) else records[0][0]
            self._logger.info(f"Found pending job #{job_id} for {ip}. Starting transfer thread.")
            # Start transfer in a daemon thread so it doesn't block the caller
            threading.Thread(target=self._send_stl_to_node, args=(ip, job_id), daemon=True).start()

    def _send_stl_to_node(self, ip, job_id):
        """
        Reads the dummy STL file and sends it to the node using CoAP Block1.
        Relies on CoAPthon's native BlockLayer to automatically chunk the file.
        """
        # Path to the dummy file used for simulation
        file_path = os.path.join(os.path.dirname(__file__), 'stl', 'false_stl.bin')
        
        try:
            with open(file_path, 'rb') as f:
                payload = f.read()
        except Exception as e:
            self._logger.error(f"Failed to read dummy STL file: {e}")
            self._db.execute("UPDATE Print SET status = 'ERROR' WHERE id = %s", (job_id,))
            return

        # Instantiate HelperClient. Ensure IP is parsed as a string for safety.
        client = HelperClient(server=(str(ip), 5683))
        try:
            self._logger.info(f"Initiating CoAP Block-wise transfer to {ip} ({len(payload)} bytes).")

            # We use the low-level request
            request = Request()
            request.code = defines.Codes.POST.number
            request.uri_path = "print"
            
            # We pass the entire file to the payload, without fragmenting it by hand.
            request.payload = payload  
            request.destination = (str(ip), 5683)

            # Let's create the initial Block1 option.
            # Let's just set szx=2 (which indicates 64-byte chunks).
            # The CoAPthon engine will automatically calculate the 'num' and 'm' flags for the various packets.
            block1_opt = Option()
            block1_opt.number = defines.OptionRegistry.BLOCK1.number
            block1_opt.value = 2  
            request.add_option(block1_opt)

            self._logger.debug("Handing over full payload to CoAPthon BlockLayer...")
            
            # Send the custom request using the protocol below (prevents HelperClient from overriding our logic)
            response = client.send_request(request, timeout=30)

            # Verify that Contiki has accepted the block
            # 68 = 2.04 Changed (End), 95 = 2.31 Continue (Another block coming)
            if response is None:
                raise Exception("Timeout waiting for final ACK")
            
            # 68 corrisponde al codice CoAP 2.04 (Changed), indicando che tutto il file è stato ricevuto
            if response.code != 68:
                raise Exception(f"Transfer failed with final code {response.code}")

            self._logger.info(f"Final ACK received from {ip}. Print started successfully.")

            # Update DB to PRINTING
            self._db.execute("UPDATE Print SET status = 'PRINTING' WHERE id = %s", (job_id,))
            self._monitor.set_node_printing(ip)

        except Exception as e:
            self._logger.error(f"Transfer failed for {ip}: {e}")
            self._db.execute("UPDATE Print SET status = 'ERROR' WHERE id = %s", (job_id,))
        finally:
            client.stop()
    
    def handle_print_finished(self, ip, result):
        """
        Manages the reception of the FINISHED or FAILED payload from the node,
        updates the Print table row, and releases the queue.
        """        
        query = "SELECT id FROM Print WHERE ip = %s AND status = 'PRINTING'"
        records = self._db.execute(query, (ip,))
        
        if not records:
            self._logger.error(f"Error: Received notification {result} from {ip}, but no PRINTING status print job found in the database!")
            self._monitor._update_db_status(ip, "ONLINE")
            with self._monitor._lock:
                if ip in self._monitor._nodes:
                    self._monitor._nodes[ip]["status"] = "ONLINE"
            if self._monitor.on_change_callback:
                self._monitor.on_change_callback(self._monitor.get_all_nodes())
            return False
            
        # Let's check that there is ONLY ONE print in PRINTING
        if len(records) > 1:
            self._logger.error(f"CRITICAL ERROR: Found {len(records)} jobs in PRINTING state for node {ip}!")
            # We put them back on the QUEUE instead of letting them fail
            for extra_record in records[1:]:
                extra_id = extra_record.get('id') if isinstance(extra_record, dict) else extra_record[0]
                self._db.execute("UPDATE Print SET status = 'QUEUE' WHERE id = %s", (extra_id,))
                self._logger.info(f"Print #{extra_id} for node {ip} reverted to QUEUE status.")

        # Let's take the first valid ID
        job_id = records[0].get('id') if isinstance(records[0], dict) else records[0][0]
        
        update_query = "UPDATE Print SET status = %s WHERE id = %s"
        self._db.execute(update_query, (result, job_id))
        self._logger.info(f"Print #{job_id} for node {ip} updated to status: {result}")
        
        status_changed = False
        
        if result != "ERROR":
            self._monitor._update_db_status(ip, "ONLINE")
            with self._monitor._lock:
                if ip in self._monitor._nodes:
                    if self._monitor._nodes[ip]["status"] != "ONLINE":
                        self._monitor._nodes[ip]["status"] = "ONLINE"
                        status_changed = True
        else:
            self._monitor._update_db_status(ip, "OFFLINE")
            with self._monitor._lock:
                if ip in self._monitor._nodes:
                    if self._monitor._nodes[ip]["status"] != "OFFLINE":
                        self._monitor._nodes[ip]["status"] = "OFFLINE"
                        status_changed = True
        
        
        if status_changed and self._monitor.on_change_callback:
             self._monitor.on_change_callback(self._monitor.get_all_nodes())
             
        self._check_and_start_print(ip)
        return True