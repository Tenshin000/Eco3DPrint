import os
import signal
import sys
import threading
import time
from configparser import ConfigParser

from backend.Database import Database
from backend.CoAPServer import CoAPServer
from backend.NodeMonitor import NodeMonitor
from backend.Socket import WebSocketManager
from backend.PrintManager import PrintManager 
from utility.Log import Log

# Initialize a logger for backend operations
logger = Log(logger_name="backend_logger", module_name="BACKEND").get_logger()

def main():
    """
    Main entry point for the Eco3DPrint backend server.
    
    Initializes the database, node monitoring, print management, WebSocket,
    and CoAP server components. Handles graceful shutdown on SIGINT.
    """
    logger.info("Starting Eco3DPrint Backend Server...")
    
    # Event used to signal threads to stop
    stop_event = threading.Event()

    # Load database configuration from file
    config = ConfigParser()
    config.read('./backend/config.ini')
    db_host = config.get('mysql', 'host')
    db_user = config.get('mysql', 'user')
    db_password = config.get('mysql', 'password')
    db_name = config.get('mysql', 'database')

    # Initialize and connect to the Database Layer
    logger.info("Starting Database...")
    db = Database(host=db_host, user=db_user, password=db_password, database=db_name)
    if not db.connect():
        logger.error("Unable to connect to the database")
        sys.exit(1)  # Fatal error if DB connection fails

    logger.info("Database Ready")

    # Reset all nodes to OFFLINE at startup to ensure consistent state
    db.execute("UPDATE Node SET status = 'OFFLINE'")

    # CoAP server configuration (host and port)
    coap_host = "fd00::1"  # IPv6 address
    coap_port = 5683       # Standard CoAP port

    # Initialize NodeMonitor to track node health and status
    node_monitor = NodeMonitor(db)
    
    # Initialize PrintManager to handle print jobs, linked with NodeMonitor
    print_manager = PrintManager(db, node_monitor)

    # Initialize WebSocketManager to broadcast node status changes
    # Pass NodeMonitor's get_all_nodes method and PrintManager
    ws_manager = WebSocketManager(node_monitor.get_all_nodes, print_manager)
    # Hook NodeMonitor's on_change_callback to broadcast updates to WebSocket clients
    node_monitor.on_change_callback = ws_manager.broadcast

    # Run WebSocket server in a separate daemon thread
    ws_thread = threading.Thread(target=ws_manager.run, daemon=True)
    ws_thread.start()
    logger.info("WebSocket Server started on port 8765")

    # Initialize CoAP server to listen for node messages (e.g., registrations, print notifications)
    server = CoAPServer(coap_host, coap_port, False, db, node_monitor, print_manager)

    def run_server():
        try:
            while not stop_event.is_set():
                server.listen(1)
        except Exception as e:
            logger.error(f"CoAP Server error: {e}")
        finally:
            logger.info("Closing CoAP Server...")
            server.close()

    # Start the CoAP server loop in a daemon thread
    coap_thread = threading.Thread(target=run_server, daemon=True)
    coap_thread.start()

    # Flag to prevent multiple shutdowns
    is_shutting_down = False

    def shutdown(sig, frame):
        nonlocal is_shutting_down
        if is_shutting_down:
            return
        is_shutting_down = True

        logger.warning("\nCTRL+C detected! Shutting down server safely...")

        # Signal threads to stop
        stop_event.set()
        if hasattr(node_monitor, '_running'):
            node_monitor._running = False

        # Stop the WebSocket event loop
        if ws_manager.loop:
            ws_manager.loop.call_soon_threadsafe(ws_manager.loop.stop)
        
        # Set all nodes to OFFLINE in the database before shutdown
        try:
            db.execute("UPDATE Node SET status = 'OFFLINE'")
            logger.info("Database: All nodes set to OFFLINE.")
        except Exception as e:
            logger.error(f"Database update failed: {e}")

        # Close CoAP server
        try:
            server.close()
        except: 
            pass

        # Close database connection
        try:
            db.close()
        except: 
            pass
        
        logger.info("Server terminated cleanly.")
        os._exit(0)  # Force exit

    # Register the shutdown handler for SIGINT
    signal.signal(signal.SIGINT, shutdown)
    
    # Keep main thread alive to handle signals and prevent exit
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()