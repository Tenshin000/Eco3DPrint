from coapthon.server.coap import CoAP
from configparser import ConfigParser

from backend.Database import Database
from backend.NodeMonitor import NodeMonitor
from backend.PrintManager import PrintManager
from .resources.NodeRegistration import NodeRegistration
from .resources.OFFSignal import OFFSignal
from .resources.PrintFinished import PrintFinished
from utility.Log import Log

class CoAPServer(CoAP):
    """Component for a Custom CoAP server with IPv6 support."""
    def __init__(self, host="::", port=5683, multicast=False, database=None, node_monitor=None, print_manager=None):
        """
        host: str, default "::" binds all IPv6 addresses
        port: int, CoAP standard UDP port 5683
        """
        super().__init__((host, port), multicast=multicast)

        self._logger = Log(
            logger_name="server_logger",
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

        # Ensure a PrintManager instance is explicitly provided
        if print_manager is None:
            self._logger.error("PrintManager instance is strictly required but was not provided.")
            raise ValueError("A PrintManager instance must be provided to CoAPServer.")
        self.print_manager = print_manager

        self.add_resource("/registration", NodeRegistration(coap_server=self, database=self._db, node_monitor=node_monitor))
        self.add_resource("/print/finished", PrintFinished(coap_server=self, print_manager=print_manager))
        self.add_resource("/signal/off", OFFSignal(coap_server=self, node_monitor=node_monitor))
        
        self._logger.info(f"CoAP Server Initialized on [{host}]:{port}...")
        